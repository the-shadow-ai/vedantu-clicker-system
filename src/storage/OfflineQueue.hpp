#pragma once
#include "app/Config.hpp"
#include "domain/ClickEvent.hpp"
#include "logging/Logger.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>

namespace svs {

/// AES-256-GCM encrypted disk queue.
///
/// Performance fix: the ofstream is held open for the entire lifetime of the
/// queue rather than being opened/closed per-write. This eliminates a kernel
/// round-trip on every click event.
class OfflineQueue {
public:
  explicit OfflineQueue(const Config &cfg) {
    std::string raw = cfg.get("OFFLINE_QUEUE_PATH",
                              "%APPDATA%/VotingSystem/offline_queue.dat");
    path_ = expandEnv(raw);
    std::filesystem::create_directories(
        std::filesystem::path(path_).parent_path());

    std::string hex_key = cfg.get(
        "OFFLINE_AES_KEY",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    try {
      if (hex_key.size() != 64) {
        throw std::invalid_argument("Key must be 64 characters");
      }
      hexToBytes(hex_key, aes_key_, 32);
    } catch (...) {
      Logger::errors()->warn("[OfflineQueue] Invalid OFFLINE_AES_KEY in config. Falling back to default key.");
      std::string fallback = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
      hexToBytes(fallback, aes_key_, 32);
    }

    // Open the append stream once — stays open until destruction.
    // ios::binary | ios::app: writes always go to end; no seek needed.
    file_out_.open(path_, std::ios::binary | std::ios::app);
  }

  ~OfflineQueue() {
    if (file_out_.is_open())
      file_out_.close();
  }

  void enqueue(const ClickEvent &ev) {
    std::lock_guard lk(mu_);
    std::string record = serialize(ev);
    appendEncrypted(record);
    Logger::perf()->debug("[OfflineQueue] Enqueued sn={}", ev.serial_no);
  }

  void flush(std::function<void(ClickEvent)> callback) {
    std::lock_guard lk(mu_);

    if (!std::filesystem::exists(path_))
      return;

    // Flush pending writes before reading
    if (file_out_.is_open())
      file_out_.flush();

    std::ifstream f(path_, std::ios::binary);
    if (!f)
      return;

    std::vector<std::string> records;
    while (f.good()) {
      uint32_t len = 0;
      if (!f.read(reinterpret_cast<char *>(&len), 4))
        break;

      std::vector<uint8_t> iv(12), tag(16), ct(len);
      f.read(reinterpret_cast<char *>(iv.data()), 12);
      f.read(reinterpret_cast<char *>(tag.data()), 16);
      f.read(reinterpret_cast<char *>(ct.data()), len);
      if (f.fail())
        break;

      std::string plaintext;
      if (decrypt(iv, tag, ct, plaintext))
        records.push_back(plaintext);
    }
    f.close();

    // Close the write stream, delete file, reopen for future events
    if (file_out_.is_open())
      file_out_.close();
    std::filesystem::remove(path_);
    file_out_.open(path_, std::ios::binary | std::ios::app);

    for (auto &rec : records) {
      auto ev = deserialize(rec);
      if (ev)
        callback(*ev);
    }
    Logger::perf()->info("[OfflineQueue] Flushed {} events", records.size());
  }

  [[nodiscard]] bool hasEvents() const {
    return std::filesystem::exists(path_) &&
           std::filesystem::file_size(path_) > 0;
  }

  void clear() {
    std::lock_guard lk(mu_);
    if (file_out_.is_open())
      file_out_.close();
    std::error_code ec;
    bool existed = std::filesystem::exists(path_);
    std::filesystem::remove(path_, ec);
    if (existed && !ec)
      Logger::perf()->info("[OfflineQueue] Pre-session offline data cleared");
    // Reopen for future use
    file_out_.open(path_, std::ios::binary | std::ios::app);
  }

private:
  static std::string serialize(const ClickEvent &ev) {
    return std::to_string(ev.serial_no) + "|" + ev.device_sn + "|" +
           std::to_string(ev.value) + "|" + std::to_string(ev.timestamp_ms) +
           "|" + ev.nonce;
  }

  static std::optional<ClickEvent> deserialize(const std::string &s) {
    try {
      std::vector<std::string> parts;
      std::istringstream ss(s);
      std::string tok;
      while (std::getline(ss, tok, '|'))
        parts.push_back(tok);
      if (parts.size() < 5)
        return std::nullopt;

      ClickEvent ev;
      ev.serial_no = std::stoull(parts[0]);
      ev.device_sn = parts[1];
      ev.value = std::stoi(parts[2]);
      ev.timestamp_ms = std::stoll(parts[3]);
      ev.nonce = parts[4];
      return ev;
    } catch (...) {
      return std::nullopt;
    }
  }

  void appendEncrypted(const std::string &plaintext) {
    std::vector<uint8_t> iv(12);
    RAND_bytes(iv.data(), 12);

    std::vector<uint8_t> ct(plaintext.size());
    std::vector<uint8_t> tag(16);
    encrypt(iv,
            {reinterpret_cast<const uint8_t *>(plaintext.data()),
             plaintext.size()},
            ct, tag);

    uint32_t len = static_cast<uint32_t>(ct.size());
    file_out_.write(reinterpret_cast<char *>(&len), 4);
    file_out_.write(reinterpret_cast<char *>(iv.data()), 12);
    file_out_.write(reinterpret_cast<char *>(tag.data()), 16);
    file_out_.write(reinterpret_cast<char *>(ct.data()), ct.size());
    // Periodic flush — every write is visible on disk within 1 frame
    file_out_.flush();
  }

  void encrypt(const std::vector<uint8_t> &iv, std::span<const uint8_t> pt,
               std::vector<uint8_t> &ct, std::vector<uint8_t> &tag) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, aes_key_, iv.data());
    int len = 0;
    EVP_EncryptUpdate(ctx, ct.data(), &len, pt.data(), (int)pt.size());
    EVP_EncryptFinal_ex(ctx, ct.data() + len, &len);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data());
    EVP_CIPHER_CTX_free(ctx);
  }

  bool decrypt(const std::vector<uint8_t> &iv, const std::vector<uint8_t> &tag,
               const std::vector<uint8_t> &ct, std::string &out) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, aes_key_, iv.data());
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
                        const_cast<uint8_t *>(tag.data()));
    out.resize(ct.size());
    int len = 0;
    EVP_DecryptUpdate(ctx, reinterpret_cast<uint8_t *>(out.data()), &len,
                      ct.data(), (int)ct.size());
    int final_len = 0;
    bool ok =
        EVP_DecryptFinal_ex(ctx, reinterpret_cast<uint8_t *>(out.data()) + len,
                            &final_len) > 0;
    EVP_CIPHER_CTX_free(ctx);
    out.resize(len + final_len);
    return ok;
  }

  static void hexToBytes(const std::string &hex, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n && i * 2 + 1 < hex.size(); ++i)
      out[i] =
          static_cast<uint8_t>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
  }

  static std::string expandEnv(const std::string &s) {
    char buf[MAX_PATH]{};
    ExpandEnvironmentStringsA(s.c_str(), buf, MAX_PATH);
    return buf;
  }

  std::string path_;
  uint8_t aes_key_[32]{};
  mutable std::mutex mu_;
  std::ofstream file_out_; ///< Held open for lifetime — avoids per-write open/close
};

} // namespace svs
