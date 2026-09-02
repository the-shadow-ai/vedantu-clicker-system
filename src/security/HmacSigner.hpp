#pragma once
#include <iomanip>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <sstream>
#include <stdexcept>
#include <string>


namespace svs {

/// Computes HMAC-SHA256 of a message using the given key.
/// Returns the result as a lowercase hex string.
class HmacSigner {
public:
  explicit HmacSigner(std::string secret) : secret_(std::move(secret)) {}

  [[nodiscard]] std::string sign(const std::string &message) const {
    unsigned char result[EVP_MAX_MD_SIZE]{};
    unsigned int len = 0;

    auto *h =
        HMAC(EVP_sha256(), secret_.data(), static_cast<int>(secret_.size()),
             reinterpret_cast<const unsigned char *>(message.data()),
             message.size(), result, &len);

    if (!h)
      throw std::runtime_error("HMAC-SHA256 failed");

    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i)
      oss << std::hex << std::setw(2) << std::setfill('0')
          << static_cast<int>(result[i]);

    return oss.str();
  }

private:
  std::string secret_;
};

/// Generates a UUID v4 (random) as a standard hex-dash string.
/// Uses OpenSSL RAND_bytes for cryptographic quality.
class NonceGenerator {
public:
  [[nodiscard]] static std::string generate() {
    unsigned char bytes[16]{};
    if (RAND_bytes(bytes, sizeof(bytes)) != 1)
      throw std::runtime_error("RAND_bytes failed");

    // Set version 4 and variant bits per RFC 4122
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    std::ostringstream oss;
    for (int i = 0; i < 16; ++i) {
      if (i == 4 || i == 6 || i == 8 || i == 10)
        oss << '-';
      oss << std::hex << std::setw(2) << std::setfill('0')
          << static_cast<int>(bytes[i]);
    }
    return oss.str();
  }
};

} // namespace svs
