#pragma once
#include "app/Config.hpp"
#include "logging/Logger.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace svs {

using json = nlohmann::json;

/// Synchronous HTTPS response
struct HttpResponse {
  long status_code{0};
  std::string body;
  bool ok() const noexcept { return status_code >= 200 && status_code < 300; }
};

/// Ultra-low-latency libcurl wrapper.
///
/// Key design for sub-10ms API calls and 300-classroom scale:
///
///  1. THREAD-LOCAL HANDLES — each calling thread gets its own CURL* with its
///     own persistent TCP/TLS connection. Zero lock contention. Keep-Alive
///     reused across all requests from that thread.
///
///  2. ONE-TIME OPTION SETUP — static options (TCP_NODELAY, keepalive, SSL,
///     timeout, http version, callbacks) are set ONCE at handle creation.
///
///  3. TCP_NODELAY — disables Nagle's algorithm. Prevents 40ms buffering of
///     small POST payloads.
///
///  4. HTTP/2 — multiplexed streams over a single TLS session.
///
///  5. SEND/RECV BUFFER TUNING — 256 KB socket buffers for bursts.
///
///  6. PRE-WARM — call warmup() once at startup from each worker thread to
///     establish the TLS session before the first real request.
///
///  NOTE: curl_global_init / curl_global_cleanup are NOT called here.
///  They must be called exactly once by CurlGlobal in main().
class HttpClient {
public:
  explicit HttpClient(const Config &cfg) {
    cert_pinning_ = cfg.getBool("TLS_CERT_PINNING", false);
    pinned_hash_ = cfg.get("TLS_PINNED_CERT_HASH", "");
    base_url_ = cfg.get("API_BASE_URL", "");
  }

  ~HttpClient() = default;

  HttpClient(const HttpClient &) = delete;
  HttpClient &operator=(const HttpClient &) = delete;

  /// Warm up the TLS session on the CALLING thread.
  /// Call from each worker thread at startup.
  void warmup(const std::string &url) {
    try {
      get(url + "/ping", 3000);
    } catch (...) { /* ignore — just priming the socket */
    }
  }

  [[nodiscard]] HttpResponse get(const std::string &url,
                                 long timeout_ms = 8000) {
    return perform("GET", url, {}, timeout_ms);
  }

  // POST timeout: 4 s is enough for any healthy API endpoint and keeps the
  // retry cycle fast. Increase via API_POST_TIMEOUT_MS in .env if needed.
  [[nodiscard]] HttpResponse post(const std::string &url, const json &body,
                                  long timeout_ms = 4000) {
    return perform("POST", url, body.dump(), timeout_ms);
  }

private:
  static size_t writeCallback(char *ptr, size_t sz, size_t nmemb,
                              std::string *out) {
    out->append(ptr, sz * nmemb);
    return sz * nmemb;
  }

  /// Returns (or lazily creates) the thread-local CURL handle.
  /// Static options applied ONCE at creation — not on every request.
  CURL *getThreadHandle() {
    thread_local CURL *tl_curl = nullptr;
    thread_local bool tl_init = false;

    if (!tl_curl) {
      tl_curl = curl_easy_init();
      if (!tl_curl)
        throw std::runtime_error("curl_easy_init failed");
    }

    if (!tl_init) {
      // TCP_NODELAY: disable Nagle — critical for latency on small POSTs
      curl_easy_setopt(tl_curl, CURLOPT_TCP_NODELAY, 1L);

      // Keep-Alive: reuse TCP/TLS connection across requests
      curl_easy_setopt(tl_curl, CURLOPT_TCP_KEEPALIVE, 1L);
      curl_easy_setopt(tl_curl, CURLOPT_TCP_KEEPIDLE, 60L);
      curl_easy_setopt(tl_curl, CURLOPT_TCP_KEEPINTVL, 30L);

      // Socket buffer sizes — 256 KB for bursty click traffic
      curl_easy_setopt(tl_curl, CURLOPT_BUFFERSIZE, 262144L);
      curl_easy_setopt(tl_curl, CURLOPT_UPLOAD_BUFFERSIZE, 262144L);

      // HTTP/2 with graceful TLS fallback
      curl_easy_setopt(tl_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
      curl_easy_setopt(tl_curl, CURLOPT_PIPEWAIT, 1L);

      // TLS
      curl_easy_setopt(tl_curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
      curl_easy_setopt(tl_curl, CURLOPT_SSL_VERIFYPEER, 1L);
      curl_easy_setopt(tl_curl, CURLOPT_SSL_VERIFYHOST, 2L);

      // Keep connection alive (critical for reuse)
      curl_easy_setopt(tl_curl, CURLOPT_FORBID_REUSE, 0L);

      // Stall detection: abort if transfer speed < 10 bytes/s for 3 s.
      // Catches connections that hang instead of returning an error, which
      // would otherwise block the retry thread for the full timeout_ms.
      curl_easy_setopt(tl_curl, CURLOPT_LOW_SPEED_LIMIT, 10L);
      curl_easy_setopt(tl_curl, CURLOPT_LOW_SPEED_TIME,   3L);

      curl_easy_setopt(tl_curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(tl_curl, CURLOPT_MAXREDIRS, 3L);
      curl_easy_setopt(tl_curl, CURLOPT_WRITEFUNCTION, writeCallback);
      curl_easy_setopt(tl_curl, CURLOPT_NOSIGNAL, 1L);

      if (cert_pinning_ && !pinned_hash_.empty())
        curl_easy_setopt(tl_curl, CURLOPT_PINNEDPUBLICKEY,
                         pinned_hash_.c_str());

      tl_init = true;
    }

    return tl_curl;
  }

  HttpResponse perform(const std::string &method, const std::string &url,
                       const std::string &body, long timeout_ms) {
    CURL *curl = getThreadHandle();
    HttpResponse resp;
    resp.body.reserve(512);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    // 1.5 s connect timeout — if the server doesn’t TCP-handshake in 1.5 s
    // it’s unreachable; retry immediately instead of waiting 3 s.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1500L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);

    curl_slist *headers = nullptr;
    if (!body.empty()) {
      headers = curl_slist_append(headers, "Content-Type: application/json");
      headers = curl_slist_append(headers, "Expect:"); // no 100-continue roundtrip
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                       static_cast<long>(body.size()));
    } else {
      curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    if (headers)
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    auto t0 = std::chrono::steady_clock::now();
    CURLcode res = curl_easy_perform(curl);
    auto t1 = std::chrono::steady_clock::now();

    if (headers)
      curl_slist_free_all(headers);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, nullptr);

    if (res != CURLE_OK)
      throw std::runtime_error(std::string("curl[") + curl_easy_strerror(res) +
                               "]");

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);

    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    Logger::perf()->info("[HTTP] {} {} → {} | {}ms", method,
                         url.substr(url.rfind('/')), resp.status_code, ms);
    return resp;
  }

  bool cert_pinning_{false};
  std::string pinned_hash_;
  std::string base_url_;
};

} // namespace svs
