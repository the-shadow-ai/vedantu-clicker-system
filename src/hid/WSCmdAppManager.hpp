#pragma once
#include "app/Config.hpp"
#include "logging/Logger.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <windows.h>

namespace svs {

/// Manages the lifecycle of WSCmdApp.exe.
/// Launches it if not running, ensures config.ini is correct, and monitors it.
class WSCmdAppManager {
public:
  explicit WSCmdAppManager(const Config &cfg) {
    exe_path_ = cfg.get("WSCMDAPP_PATH", "./sdk/QTWSCmdApp.exe");
    port_ = cfg.get("WS_PORT", "9002");
  }

  ~WSCmdAppManager() { stop(); }

  /// Starts WSCmdApp.exe (if not already running). Configures config.ini first.
  bool start() {
    patchConfigIni();

    // Resolve absolute path
    std::filesystem::path exe(exe_path_);
    if (!exe.is_absolute()) {
      char buf[MAX_PATH];
      ::GetModuleFileNameA(NULL, buf, MAX_PATH);
      exe = std::filesystem::path(buf).parent_path() / exe;
    }

    if (!std::filesystem::exists(exe)) {
      Logger::errors()->error("[WSCmdAppMgr] WSCmdApp.exe not found at: {}",
                              exe.string());
      return false;
    }

    exe_abs_ = exe.string();
    exe_dir_ = exe.parent_path().string();

    spawnProcess();
    running_ = true;

    // Monitor thread: restart on crash
    monitor_thread_ = std::thread([this] {
      while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (running_ && !isAlive()) {
          Logger::errors()->warn(
              "[WSCmdAppMgr] WSCmdApp.exe crashed, restarting...");
          spawnProcess();
        }
      }
    });

    Logger::hid()->info("[WSCmdAppMgr] WSCmdApp.exe launched from: {}",
                        exe_abs_);
    return true;
  }

  void stop() {
    running_ = false;
    if (monitor_thread_.joinable()) {
      monitor_thread_.join();
    }
    if (pi_.hProcess && pi_.hProcess != INVALID_HANDLE_VALUE) {
      ::TerminateProcess(pi_.hProcess, 0);
      ::CloseHandle(pi_.hProcess);
      ::CloseHandle(pi_.hThread);
      pi_ = {};
    }
  }

private:
  void patchConfigIni() {
    // Write a minimal config.ini so WSCmdApp runs as server on our port
    std::filesystem::path exe(exe_path_);
    if (!exe.is_absolute()) {
      char buf[MAX_PATH];
      ::GetModuleFileNameA(NULL, buf, MAX_PATH);
      exe = std::filesystem::path(buf).parent_path() / exe;
    }

    auto cfg_path = exe.parent_path() / "config.ini";
    std::ofstream f(cfg_path);
    if (!f.is_open()) {
      Logger::errors()->warn("[WSCmdAppMgr] Could not write config.ini at {}",
                             cfg_path.string());
      return;
    }
    f << "[sdk]\n"
      << "role=server\n"
      << "port=" << port_ << "\n"
      << "heartBeatOnOff=0\n"
      << "WSExit=1\n"
      << "language=1\n" // English
      << "jsonBase=1\n" // CRITICAL: 1 = JSON over WebSocket, 0 = Hex String
      << "logUpload=0\n";
  }

  void spawnProcess() {
    if (pi_.hProcess && pi_.hProcess != INVALID_HANDLE_VALUE) {
      ::TerminateProcess(pi_.hProcess, 0);
      ::CloseHandle(pi_.hProcess);
      ::CloseHandle(pi_.hThread);
      pi_ = {};
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // run completely hidden

    std::string cmd = "\"" + exe_abs_ + "\"";
    if (!::CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW | NORMAL_PRIORITY_CLASS, nullptr,
                          exe_dir_.c_str(), &si, &pi_)) {
      Logger::errors()->error("[WSCmdAppMgr] CreateProcess failed: {}",
                              ::GetLastError());
    }
  }

  bool isAlive() const {
    if (!pi_.hProcess)
      return false;
    DWORD code = 0;
    ::GetExitCodeProcess(pi_.hProcess, &code);
    return code == STILL_ACTIVE;
  }

  std::string exe_path_;
  std::string exe_abs_;
  std::string exe_dir_;
  std::string port_;
  PROCESS_INFORMATION pi_{};
  std::atomic<bool> running_{false};
  std::thread monitor_thread_;
};

} // namespace svs
