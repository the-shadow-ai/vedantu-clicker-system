#pragma once
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace svs {

/// Loads key=value pairs from a .env file and provides typed accessors.
///
/// Key lookup priority (highest to lowest):
///   1. Real environment variables (set in the OS / process)
///   2. .env file entries
///   3. Default value passed by the caller
///
/// Throws std::runtime_error if a required key is missing from both sources.
class Config {
public:
  explicit Config(const std::filesystem::path &env_path) {
    if (!std::filesystem::exists(env_path)) {
      throw std::runtime_error(
          "Configuration file not found: " + env_path.string() +
          "\nCopy .env.example to .env and fill in your credentials.");
    }
    load(env_path);
  }

  // ── Required getters (throw if missing) ─────────────────────────────────

  [[nodiscard]] std::string require(const std::string &key) const {
    if (const char *val = ::getenv(key.c_str()); val != nullptr)
      return val;
    if (auto it = data_.find(key); it != data_.end())
      return it->second;
    throw std::runtime_error("Required config key missing: " + key);
  }

  [[nodiscard]] int requireInt(const std::string &key) const {
    return std::stoi(require(key));
  }

  // ── Optional getters (return default if missing) ──────────────────────

  [[nodiscard]] std::string get(const std::string &key,
                                const std::string &def = "") const {
    if (const char *val = ::getenv(key.c_str()); val != nullptr)
      return val;
    if (auto it = data_.find(key); it != data_.end())
      return it->second;
    return def;
  }

  [[nodiscard]] int getInt(const std::string &key, int def = 0) const {
    auto s = get(key);
    return s.empty() ? def : std::stoi(s);
  }

  [[nodiscard]] bool getBool(const std::string &key, bool def = false) const {
    auto s = get(key);
    if (s.empty())
      return def;
    return (s == "true" || s == "1" || s == "yes");
  }

  /// Validate that all required keys are present; throws on first failure.
  void validateRequired(std::initializer_list<const char *> keys) const {
    for (auto *k : keys)
      require(k);
  }

  /// Runtime override — used by SdkExtractor to inject the extracted path.
  /// Takes precedence over both environment variables and the .env file.
  void set(const std::string &key, const std::string &value) {
    data_[key] = value;
  }

private:
  void load(const std::filesystem::path &path) {
    std::ifstream f(path);
    if (!f.is_open())
      throw std::runtime_error("Cannot open config file: " + path.string());

    std::string line;
    while (std::getline(f, line)) {
      // Strip CRLF
      if (!line.empty() && line.back() == '\r')
        line.pop_back();

      // Skip blanks and comments
      if (line.empty() || line[0] == '#')
        continue;

      auto eq = line.find('=');
      if (eq == std::string::npos)
        continue;

      std::string key   = trim(line.substr(0, eq));
      std::string value = trim(line.substr(eq + 1));

      // Strip inline comments
      if (auto hash = value.find(" #"); hash != std::string::npos)
        value = trim(value.substr(0, hash));

      if (!key.empty())
        data_[key] = value;
    }
  }

  static std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
  }

  std::unordered_map<std::string, std::string> data_;
};

} // namespace svs
