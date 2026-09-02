// =============================================================================
//  Vedantu Clicker System — Main Entry Point
//
//  Architecture:
//    WSCmdApp.exe (SunVote SDK) ──WS──► ClickerClient
//                                              │
//                                    LockFreeQueue<ClickEvent>
//                                              │
//                                       EventDispatcher ──HTTPS──► Vedantu API
//                                              │
//                                         UiState ──► MainWindow (ImGui/D3D11)
// =============================================================================
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <atomic>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <windows.h>

// Application layers
#include "app/Config.hpp"
#include "domain/ClickEvent.hpp"
#include "hid/ClickerClient.hpp"
#include "hid/SdkExtractor.hpp"
#include "hid/WSCmdAppManager.hpp"
#include "logging/LatencyLogger.hpp"
#include "logging/Logger.hpp"
#include "network/CurlGlobal.hpp"
#include "network/EventDispatcher.hpp"
#include "network/HttpClient.hpp"
#include "network/SessionManager.hpp"
#include "queue/LockFreeQueue.hpp"
#include "security/HmacSigner.hpp"
#include "storage/OfflineQueue.hpp"
#include "ui/MainWindow.hpp"
#include "ui/UiState.hpp"

using namespace svs;

// =============================================================================
//  WinMain — Windows entry point (WIN32 subsystem, no console window)
// =============================================================================
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

  // ── 0. libcurl global init (RAII) ─────────────────────────────────────────
  //  Must happen before any HttpClient is constructed.
  CurlGlobal curl_global;

  // ── 1. Load configuration (.env next to the executable) ───────────────────
  char exe_buf[MAX_PATH];
  ::GetModuleFileNameA(NULL, exe_buf, MAX_PATH);
  auto exe_dir  = std::filesystem::path(exe_buf).parent_path();
  auto cfg_path = exe_dir / ".env";

  std::unique_ptr<Config> cfg;
  try {
    cfg = std::make_unique<Config>(cfg_path);
    cfg->validateRequired({"API_BASE_URL", "API_POST_ENDPOINT",
                           "SESSION_BASE_URL", "SESSION_ENDPOINT",
                           "SESSION_SECRET", "WSCMDAPP_PATH"});
  } catch (const std::exception &e) {
    ::MessageBoxA(nullptr,
                  (std::string("Configuration error:\n") + e.what()).c_str(),
                  "Vedantu Clicker System — Fatal Error",
                  MB_ICONERROR | MB_OK);
    return 1;
  }

  // ── 2. Initialise logging ──────────────────────────────────────────────────
  Logger::init(*cfg);
  Logger::session()->info("=== Vedantu Clicker System starting ===");
  Logger::session()->info("Config loaded from: {}", cfg_path.string());

  latencyLog().open(exe_dir.string());

  // ── 3. Shared state objects ────────────────────────────────────────────────
  auto queue    = std::make_unique<LockFreeQueue<ClickEvent>>();
  auto offline  = std::make_unique<OfflineQueue>(*cfg);
  // Single shared HttpClient — all network calls share one connection pool.
  auto http     = std::make_unique<HttpClient>(*cfg);
  auto session  = std::make_unique<SessionManager>(*cfg, *http);
  auto ui_state = std::make_unique<UiState>();

  // ── 4. Event dispatcher — session-gated concurrent API posting ─────────────
  auto dispatcher = std::make_unique<EventDispatcher>(
      *cfg, *session, *queue, *offline, *http, *ui_state);
  dispatcher->start();

  // ── 5. HID layer — extract embedded SDK, then launch WSCmdApp.exe ──────────
  std::unique_ptr<SdkExtractor> sdk_extractor;
  try {
    sdk_extractor = std::make_unique<SdkExtractor>();
    cfg->set("WSCMDAPP_PATH", sdk_extractor->wscmdapp_path());
    Logger::session()->info("[Main] SDK extracted to temp: {}",
                            sdk_extractor->wscmdapp_path());
  } catch (const std::exception &e) {
    Logger::errors()->error("[Main] SDK extraction failed: {}", e.what());
    ::MessageBoxA(nullptr,
                  (std::string("Could not extract the embedded SDK:\n") +
                   e.what() + "\n\nThe application cannot start.")
                      .c_str(),
                  "Vedantu Clicker System — Fatal Error",
                  MB_ICONERROR | MB_OK);
    return 1;
  }

  auto ws_mgr = std::make_unique<WSCmdAppManager>(*cfg);
  if (!ws_mgr->start()) {
    Logger::errors()->error("Failed to start WSCmdApp.exe from extracted path");
    ::MessageBoxA(nullptr,
                  "Could not start the SunVote WSCmdApp.exe.\n"
                  "The embedded SDK may be blocked by antivirus.\n"
                  "Please whitelist this application and try again.",
                  "Vedantu Clicker System — HID Error",
                  MB_ICONWARNING | MB_OK);
  }

  // Give WSCmdApp 1.5 s to start its WebSocket server
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  auto clicker = std::make_unique<ClickerClient>(
      *cfg,
      // On click: push to UI (sub-10 ms) AND lock-free queue (for API)
      [&](ClickEvent ev) {
        // Stamp entry timestamp — first CPU nanosecond on this thread
        ev.timestamp_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();

        latencyLog().recordEntry(ev.serial_no, ev.device_sn,
                                 ev.value, ev.timestamp_ms);

        ui_state->addEvent(ev); // bypasses dispatcher — sub-10 ms UI latency

        if (!queue->push(ev)) {
          offline->enqueue(ev);
          Logger::errors()->warn("[Main] Queue full! Event sn={} sent offline",
                                 ev.serial_no);
        } else {
          dispatcher->notifyPush(); // wake dispatcher — latency ≈ 0
        }
      },
      // On dongle status change
      [&](bool connected) {
        ui_state->dongle_connected.store(connected);
        Logger::hid()->info("[Main] Dongle: {}",
                            connected ? "CONNECTED" : "DISCONNECTED");
      },
      // On clicker paired (during pairing window)
      [&](RegisteredClicker rc) {
        ui_state->addRegisteredClicker(std::move(rc));
        Logger::hid()->info("[Main] Clicker paired: {}", rc.sn);
      },
      // On pairing session start: clear previous list
      [&]() { ui_state->clearRegistered(); });

  clicker->start();

  // ── 6. UI — Win32 + D3D11 + ImGui window ──────────────────────────────────
  MainWindow window(
      *ui_state,
      // Session validate callback
      [&](std::string session_id) {
        Logger::session()->info("[Main] Validate requested for sessionId={}",
                                session_id);

        // Reset session_start_ms_ so the dispatcher purges stale events
        // on EVERY validate, not just the first one.
        // This covers the operator pressing VALIDATE a second time mid-quiz.
        dispatcher->resetSession();

        bool ok = session->validate(session_id);
        if (ok) {
          ui_state->updateSession(session->getInfo());
          ui_state->validation_error.store(false);

          // ── Pre-warm the API connection on a detached thread ───────────────
          std::thread([&http = *http,
                       api_base = cfg->require("API_BASE_URL")] {
            Logger::api()->info("[Main] Pre-warming API connection...");
            http.warmup(api_base);
            Logger::api()->info("[Main] API connection pre-warmed.");
          }).detach();

        } else {
          // Show the error inline in the UI — no blocking modal popup.
          // MessageBoxA steals focus and freezes the entire app; the in-app
          // banner disappears automatically on the next successful validate.
          ui_state->validation_error.store(true);
          Logger::session()->warn("[Main] Session validation failed for id={}",
                                  session_id);
        }
      },
      // Registration start/stop toggle from UI button
      [&](bool start) {
        ui_state->pairing_active.store(start);
        if (start)
          clicker->startPairing();
        else
          clicker->stopPairing();
      });

  if (!window.create("Vedantu Clicker System")) {
    Logger::errors()->critical("Failed to create main window");
    return 1;
  }

  Logger::session()->info("UI window created — entering render loop");
  window.runLoop();

  // ── 7. Graceful shutdown ───────────────────────────────────────────────────
  Logger::session()->info("Shutting down...");
  dispatcher->stop();
  clicker->stop();
  ws_mgr->stop();
  latencyLog().shutdown();
  Logger::shutdown();

  return 0;
}
