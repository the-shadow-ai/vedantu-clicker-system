#pragma once
// =============================================================================
//  CurlGlobal.hpp
//  Process-wide libcurl initialisation RAII wrapper.
//  Must be instantiated EXACTLY ONCE in main() before any HttpClient is made.
// =============================================================================
#include <curl/curl.h>
#include <stdexcept>

namespace svs {

class CurlGlobal {
public:
  CurlGlobal() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
      throw std::runtime_error("curl_global_init failed");
  }
  ~CurlGlobal() { curl_global_cleanup(); }

  CurlGlobal(const CurlGlobal &) = delete;
  CurlGlobal &operator=(const CurlGlobal &) = delete;
};

} // namespace svs
