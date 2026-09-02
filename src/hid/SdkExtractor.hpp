#pragma once
// =============================================================================
//  SdkExtractor.hpp
//  Embeds all SunVote SDK binaries as RCDATA Windows resources inside the exe.
//  On construction, extracts them silently to %TEMP%\vedantu_svs\.
//  On destruction, terminates QTWSCmdApp if running and wipes the temp tree.
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "hid/resource.h"
#include "logging/Logger.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <tlhelp32.h>
#include <vector>
#include <windows.h>

namespace svs {

class SdkExtractor {
public:
  // Extracts all SDK files to %TEMP%\vedantu_svs\ and returns the
  // absolute path to QTWSCmdApp.exe.
  // Throws std::runtime_error on any failure.
  explicit SdkExtractor() {
    // Build temp dir path: %TEMP%\vedantu_svs
    char temp_buf[MAX_PATH];
    ::GetTempPathA(MAX_PATH, temp_buf);
    sdk_dir_ = std::filesystem::path(temp_buf) / "vedantu_svs";

    // Kill any leftover QTWSCmdApp.exe from a previous run using Win32 APIs
    // directly — no cmd.exe / shell process is spawned, so no console window
    // flashes on startup.
    killProcessByName(L"QTWSCmdApp.exe");
    // Give the OS time to release file handles before we try to delete the dir
    ::Sleep(400);

    // Clean up or use a unique directory if locked
    if (std::filesystem::exists(sdk_dir_)) {
      std::error_code ec;
      std::filesystem::remove_all(sdk_dir_, ec);
      if (ec) {
        // Fallback: use a unique directory name if the old one is locked
        auto ts = std::chrono::system_clock::now().time_since_epoch().count();
        sdk_dir_ = std::filesystem::path(temp_buf) /
                   ("vedantu_svs_" + std::to_string(ts));
      }
    }
    std::filesystem::create_directories(sdk_dir_);
    std::filesystem::create_directories(sdk_dir_ / "log");
    std::filesystem::create_directories(sdk_dir_ / "voice");

    // Extract each embedded resource to a file
    struct Entry {
      int id;
      const char *name;
    };
    static const Entry entries[] = {
        {IDR_SDK_WSCMDAPP, "QTWSCmdApp.exe"},
        {IDR_SDK_QTZT, "QTZT_SDK_V4.dll"},
        {IDR_SDK_QT5CORE, "Qt5Core.dll"},
        {IDR_SDK_QT5NET, "Qt5Network.dll"},
        {IDR_SDK_QT5WS, "Qt5WebSockets.dll"},
        {IDR_SDK_HIDAPI, "hidapi.dll"},
        {IDR_SDK_LIBCURL, "libcurl.dll"},
        {IDR_SDK_LOG4CPLUS, "log4cplusU.dll"},
        {IDR_SDK_QUAZIP, "quazip1-qt5.dll"},
        {IDR_SDK_ZLIB, "zlib.dll"},
        {IDR_SDK_RESOURCES, "SDK_V4_resources.rcc"},
        {IDR_SDK_CONFIGINI, "config.ini"},
        {IDR_SDK_LOGPROP, "log.properties"},
        {IDR_SDK_MSVCP, "MSVCP140.dll"},
        {IDR_SDK_VCRUNTIME, "VCRUNTIME140.dll"},
        {IDR_SDK_MSVCP1, "MSVCP140_1.dll"},
        {IDR_SDK_MSVCP2, "MSVCP140_2.dll"},
        {IDR_SDK_VCCORLIB, "vccorlib140.dll"},
        {IDR_SDK_CONCRT, "concrt140.dll"},
        {IDR_SDK_ATOMIC, "msvcp140_atomic_wait.dll"},
        {IDR_SDK_CODECVT, "msvcp140_codecvt_ids.dll"},
    };

    HMODULE hMod = ::GetModuleHandleA(nullptr);
    for (auto &e : entries)
      extractOne(hMod, e.id, sdk_dir_ / e.name);

    exe_path_ = (sdk_dir_ / "QTWSCmdApp.exe").string();
    Logger::hid()->info("[SdkExtractor] Extracted to: {}", sdk_dir_.string());
  }

  ~SdkExtractor() {
    // Best-effort cleanup — remove temp tree
    try {
      std::filesystem::remove_all(sdk_dir_);
    } catch (...) {
    }
  }

  // Returns the absolute path to the extracted QTWSCmdApp.exe
  [[nodiscard]] const std::string &wscmdapp_path() const { return exe_path_; }

private:
  /// Terminates all processes whose executable name matches \p name
  /// (case-insensitive). Uses only Win32 APIs — never spawns a CMD window.
  static void killProcessByName(const wchar_t *name) {
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
      return;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (::Process32FirstW(snap, &pe)) {
      do {
        if (::_wcsicmp(pe.szExeFile, name) == 0) {
          HANDLE hProc =
              ::OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
          if (hProc) {
            ::TerminateProcess(hProc, 1);
            ::CloseHandle(hProc);
          }
        }
      } while (::Process32NextW(snap, &pe));
    }
    ::CloseHandle(snap);
  }

  static void extractOne(HMODULE hMod, int id,
                         const std::filesystem::path &dest) {
    HRSRC hRsrc = ::FindResourceW(hMod, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!hRsrc)
      throw std::runtime_error(std::string("FindResource failed for id ") +
                               std::to_string(id) +
                               " error=" + std::to_string(::GetLastError()));

    HGLOBAL hGlob = ::LoadResource(hMod, hRsrc);
    if (!hGlob)
      throw std::runtime_error("LoadResource failed for id " +
                               std::to_string(id));

    const void *data = ::LockResource(hGlob);
    DWORD size = ::SizeofResource(hMod, hRsrc);
    if (!data || !size)
      throw std::runtime_error("LockResource/SizeofResource failed for id " +
                               std::to_string(id));

    std::ofstream f(dest, std::ios::binary | std::ios::trunc);
    if (!f.is_open())
      throw std::runtime_error("Cannot write extracted file: " + dest.string());
    f.write(static_cast<const char *>(data), size);
    if (!f)
      throw std::runtime_error("Write error for: " + dest.string());
  }

  std::filesystem::path sdk_dir_;
  std::string exe_path_;
};

} // namespace svs
