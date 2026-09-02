#pragma once
#include "app/Config.hpp"
#include "logging/Logger.hpp"
#include "network/HttpClient.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>

namespace svs {

struct SessionInfo {
  std::string session_id;
  std::string title;
  std::string presenter;
  int64_t start_time_ms{0};
  int64_t end_time_ms{0};
  bool valid{false};
};

/// Validates a session ID against the Vedantu scheduling API and caches the
/// token. Thread-safe. Expiry is checked on every API dispatch.
class SessionManager {
public:
  SessionManager(const Config &cfg, HttpClient &http)
      : http_(http), base_url_(cfg.require("SESSION_BASE_URL")),
        endpoint_(cfg.require("SESSION_ENDPOINT")),
        secret_(cfg.require("SESSION_SECRET")) {}

  /// Validates sessionId against the GET API. Blocks until done.
  /// Returns true on HTTP 2xx with a parseable response.
  bool validate(const std::string &session_id) {
    std::string url = base_url_ + endpoint_ +
                      "?sessionId=" + urlEncode(session_id) +
                      "&secret=" + urlEncode(secret_);

    Logger::session()->info("[Session] Validating sessionId={}", session_id);

    try {
      auto resp = http_.get(url, 10000);

      if (!resp.ok()) {
        Logger::session()->warn("[Session] Validation failed HTTP {}: {}",
                                resp.status_code, resp.body);
        std::lock_guard lk(mu_);
        info_.valid = false;
        return false;
      }

      auto j = json::parse(resp.body);

      SessionInfo si;
      si.session_id = session_id;
      si.title = j.value("title", "");
      si.presenter = j.value("presenter", "");
      si.start_time_ms = j.value("startTime", int64_t{0});
      si.end_time_ms = j.value("endTime", int64_t{0});
      si.valid = true;

      {
        std::lock_guard lk(mu_);
        info_ = si;
      }

      Logger::session()->info(
          "[Session] Valid! title='{}' presenter='{}' start={} end={}",
          si.title, si.presenter, si.start_time_ms, si.end_time_ms);

      return true;
    } catch (const std::exception &e) {
      Logger::errors()->error("[Session] Exception during validation: {}",
                              e.what());
      std::lock_guard lk(mu_);
      info_.valid = false;
      return false;
    }
  }

  [[nodiscard]] bool isValid() const {
    std::lock_guard lk(mu_);
    return info_.valid;
  }

  [[nodiscard]] SessionInfo getInfo() const {
    std::lock_guard lk(mu_);
    return info_;
  }

  void invalidate() {
    std::lock_guard lk(mu_);
    info_.valid = false;
  }

private:
  static std::string urlEncode(const std::string &s) {
    // Simple percent-encoding for query params
    std::string result;
    result.reserve(s.size() * 2);
    for (unsigned char c : s) {
      if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
        result += static_cast<char>(c);
      } else {
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%%%02X", c);
        result += buf;
      }
    }
    return result;
  }

  HttpClient &http_;
  std::string base_url_;
  std::string endpoint_;
  std::string secret_;
  mutable std::mutex mu_;
  SessionInfo info_;
};

} // namespace svs
