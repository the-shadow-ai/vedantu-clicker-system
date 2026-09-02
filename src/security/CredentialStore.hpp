#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <wincred.h>
#include <windows.h>

#pragma comment(lib, "Advapi32.lib")

namespace svs {

/// Retrieves secrets from Windows Credential Manager or environment variables.
/// Priority: environment variable > Windows Credential Manager
class CredentialStore {
public:
  /// Try to get a secret by env var name first, then WinCred target name.
  [[nodiscard]] static std::optional<std::string>
  get(const std::string &env_var, const std::string &wincred_target) {
    // 1. Check environment variable
    if (const char *val = ::getenv(env_var.c_str()); val && *val != '\0')
      return val;

    // 2. Check Windows Credential Manager
    PCREDENTIALW cred = nullptr;
    std::wstring target(wincred_target.begin(), wincred_target.end());
    if (::CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred)) {
      std::string secret(reinterpret_cast<char *>(cred->CredentialBlob),
                         cred->CredentialBlobSize);
      ::CredFree(cred);
      return secret;
    }

    return std::nullopt;
  }

  /// Get a secret or throw if not found.
  [[nodiscard]] static std::string require(const std::string &env_var,
                                           const std::string &wincred_target) {
    auto val = get(env_var, wincred_target);
    if (!val)
      throw std::runtime_error(
          "Required secret not found. Set env var '" + env_var +
          "' or add to Windows Credential Manager as '" + wincred_target + "'");
    return *val;
  }
};

} // namespace svs
