#pragma once
// =============================================================================
//  ClickerClient.hpp
//  WebSocket client for SunVote WSCmdApp.exe.
//
//  LATENCY DESIGN (target: UI update < 10 ms from button press)
//  ─────────────────────────────────────────────────────────────
//  • The WebSocket receive callback (parseIncoming) NEVER sleeps or blocks.
//    Any sleep on the receive thread delays every subsequent message,
//    including the next answerChoices reply.
//
//  • startChoices is sent ONCE — on the first inactive→active transition.
//    Re-firing it on every heartbeat restarts WSCmdApp's question session,
//    introducing a 500–2000 ms blackout window where clicks are discarded
//    or delayed.  This was the primary cause of the 1500–6500 ms UI delays.
//
//  • lessMode=2 ("allow repeated input, show last answer") keeps the clicker
//    LCD displaying the last chosen letter at all times. This removes the
//    need to ever restart the session to clear "Sent" from the screen.
//
//  • Pairing command sequences (which need inter-command delays) run on a
//    fire-and-forget detached thread so neither the UI thread nor the receive
//    thread is blocked.
// =============================================================================
#include "app/Config.hpp"
#include "domain/ClickEvent.hpp"
#include "hid/RegisteredClicker.hpp"
#include "logging/Logger.hpp"
#include "security/HmacSigner.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

// Minimum interval between consecutive startChoices re-sends triggered by
// late-joining or sleep-wake clickers.  Prevents a burst of keyboardOnlineOne
// events (e.g. 10 clickers all waking at once) from repeatedly restarting
// the WSCmdApp question session in quick succession.
static constexpr int64_t kStartChoicesDebounceMs = 600;

namespace svs {

using json = nlohmann::json;

/// WebSocket client that connects to WSCmdApp.exe.
///
/// Normal mode : sends startChoices (once) → receives answerChoices per click.
/// Pairing mode: sends startQuickMatch → receives keyboardOnlineOne per clicker.
class ClickerClient {
public:
  using ClickCallback    = std::function<void(ClickEvent)>;
  using StatusCallback   = std::function<void(bool /*dongle_active*/)>;
  using RegisterCallback = std::function<void(RegisteredClicker)>;
  using ClearCallback    = std::function<void()>;

  ClickerClient(const Config &cfg, ClickCallback on_click,
                StatusCallback on_status, RegisterCallback on_paired = {},
                ClearCallback on_clear = {})
      : on_click_(std::move(on_click)), on_status_(std::move(on_status)),
        on_register_(std::move(on_paired)), on_clear_(std::move(on_clear)) {
    ws_url_  = "ws://" + cfg.get("WS_HOST", "127.0.0.1") + ":" +
               cfg.get("WS_PORT", "9002");
    base_id_ = cfg.getInt("BASE_ID", 1);
  }

  ~ClickerClient() { stop(); }

  void start() {
    ws_.setUrl(ws_url_);
    ws_.disableAutomaticReconnection();
    ws_.disablePerMessageDeflate(); // eliminates per-message compression overhead
    ws_.setOnMessageCallback(
        [this](const ix::WebSocketMessagePtr &msg) { handleMessage(msg); });
    running_ = true;
    ws_.start();
    Logger::hid()->info("[ClickerClient] Connecting to WSCmdApp at {}", ws_url_);

    // Reconnect watcher: sleeps most of the time; only acts when disconnected.
    reconnect_thread_ = std::thread([this] {
      while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (running_ && !ws_connected_) {
          Logger::hid()->info("[ClickerClient] Reconnecting...");
          ws_.stop();
          std::this_thread::sleep_for(std::chrono::milliseconds(400));
          ws_.start();
        }
      }
    });

    // Heartbeat: probes dongle status every 3 s.
    // Sends readConnectBase ONLY — never re-fires startChoices.
    // startChoices is sent exactly once: on the inactive→active transition.
    heartbeat_thread_ = std::thread([this] {
      std::this_thread::sleep_for(std::chrono::seconds(4)); // let startup settle
      while (running_) {
        if (ws_connected_)
          sendReadBase();
        std::this_thread::sleep_for(std::chrono::seconds(3));
      }
    });
  }

  void stop() {
    running_ = false;
    if (ws_.getReadyState() == ix::ReadyState::Open && !base_sn_.empty()) {
      safeWsSend(json{{"fun", "stopChoices"},
                      {"baseId", base_id_},
                      {"baseSn", base_sn_},
                      {"packetTag", "1"}}
                     .dump());
      safeWsSend(json{{"fun", "stopQuickMatch"},
                      {"baseId", base_id_},
                      {"baseSn", base_sn_},
                      {"packetTag", "1"}}
                     .dump());
      // Brief delay so the stop commands are flushed before the socket closes.
      // This runs on the application shutdown thread — not the receive thread.
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      Logger::hid()->info("[ClickerClient] Shutdown: told dongle to go idle");
    }
    ws_.stop();
    if (reconnect_thread_.joinable())
      reconnect_thread_.join();
    if (heartbeat_thread_.joinable())
      heartbeat_thread_.join();
  }

  [[nodiscard]] bool isDongleConnected() const noexcept {
    return ws_connected_ && base_station_active_;
  }

  // ── Pairing ──────────────────────────────────────────────────────────────
  // Pairing sequences need short inter-command delays for WSCmdApp to process
  // each command before the next one arrives.  These delays run on a dedicated
  // detached thread so neither the calling (UI) thread nor the WS receive
  // thread is ever blocked.

  void startPairing() {
    if (!isDongleConnected()) {
      Logger::hid()->warn("[ClickerClient] startPairing: dongle not connected");
      return;
    }
    pairing_active_ = true;
    if (on_clear_)
      on_clear_();

    std::thread([this] {
      safeWsSend(json{{"fun", "stopChoices"},
                      {"baseId", base_id_},
                      {"baseSn", base_sn_},
                      {"packetTag", "1"}}
                     .dump());
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      safeWsSend(json{{"fun", "readPairMode"},
                      {"baseId", base_id_},
                      {"baseSn", base_sn_},
                      {"packetTag", "1"}}
                     .dump());
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      safeWsSend(json{{"fun", "startQuickMatch"},
                      {"baseId", base_id_},
                      {"baseSn", base_sn_},
                      {"packetTag", "1"}}
                     .dump());
      Logger::hid()->info("[ClickerClient] startQuickMatch sent — dongle is pairing");
    }).detach();
  }

  void stopPairing() {
    pairing_active_ = false;
    std::thread([this] {
      safeWsSend(json{{"fun", "stopQuickMatch"},
                      {"baseId", base_id_},
                      {"baseSn", base_sn_},
                      {"packetTag", "1"}}
                     .dump());
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      sendStartAnswering();
      Logger::hid()->info("[ClickerClient] stopQuickMatch sent — answering resumed");
    }).detach();
  }

  [[nodiscard]] bool isPairingActive() const noexcept {
    return pairing_active_.load();
  }

private:
  // ── WebSocket message dispatch ───────────────────────────────────────────
  void handleMessage(const ix::WebSocketMessagePtr &msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
      ws_connected_ = true;
      Logger::hid()->info("[ClickerClient] WebSocket connected to WSCmdApp");
      sendReadBase();
      on_status_(isDongleConnected());
    } else if (msg->type == ix::WebSocketMessageType::Close) {
      ws_connected_        = false;
      base_station_active_ = false;
      pairing_active_      = false;
      Logger::hid()->warn("[ClickerClient] WebSocket disconnected");
      on_status_(false);
    } else if (msg->type == ix::WebSocketMessageType::Message) {
      parseIncoming(msg->str); // MUST return quickly — never sleep here
    } else if (msg->type == ix::WebSocketMessageType::Error) {
      Logger::errors()->error("[ClickerClient] WS error: {}", msg->errorInfo.reason);
    }
  }

  // ── Message parser  ──────────────────────────────────────────────────────
  // Called on the ixwebsocket receive thread.
  // CONTRACT: this function must NEVER call sleep_for, sleep, or any blocking
  // wait. Doing so delays every subsequent incoming message, including the next
  // answerChoices reply from a student's button press.
  void parseIncoming(const std::string &raw) {
    Logger::hid()->info("[ClickerClient] RX: {}", raw);
    try {
      auto j = json::parse(raw);
      const std::string fun = j.value("fun", "");

      // ── Base station connect / disconnect ─────────────────────────────
      if (fun == "readConnectBase" || fun == "baseConnect" ||
          fun == "connectBase"     || fun == "baseDisconnect") {

        // Snapshot previous state to detect transitions.
        bool was_active = base_station_active_.load();

        if (fun == "baseDisconnect") {
          base_station_active_ = false;
        } else if (fun == "connectBase") {
          base_sn_             = j.value("baseSn", "");
          auto infos           = j.value("infos", json::object());
          base_station_active_ = (infos.value("value", "") == "OK");
        } else {
          // readConnectBase response: empty infos[] means dongle unplugged.
          base_sn_             = j.value("baseSn", "");
          base_station_active_ = !j.value("infos", json::array()).empty();
        }

        bool now_active = base_station_active_.load();
        Logger::hid()->info("[ClickerClient] Base active={} SN={}", now_active, base_sn_);
        on_status_(isDongleConnected());

        // Fire startChoices ONLY on a genuine inactive→active transition.
        //
        // Re-firing on every 3s heartbeat causes WSCmdApp to restart its
        // question session each time. During the restart (~2-3s), WSCmdApp
        // buffers incoming RF clicks and delivers them all at once on completion.
        // This produces the batch-delivery pattern seen in the UI table
        // (groups of events sharing the same timestamp with 2500-6500ms delays).
        //
        // lessMode=2 keeps the last answer on the clicker LCD without needing
        // to restart the session — the session stays alive indefinitely.
        if (!was_active && now_active && !pairing_active_.load())
          sendStartAnswering();

        return;
      }

      // ── Clicker pairing / late-join / sleep-wake event ───────────────────
      // Both names handled for forward compatibility with WSCmdApp versions.
      //
      // This event fires in TWO situations:
      //   1. Pairing mode  — user pressed "Pair Clickers"; forward to UI.
      //   2. Answering mode — a clicker powered on AFTER the dongle was already
      //      active (Bug 1: ~5% of clickers show IDLE), OR a sleeping clicker
      //      just woke up (Bug 2: sleep-wake not re-pairing).
      //
      // In case (2) WSCmdApp has already accepted the clicker at the RF level
      // but its question-session slot table is stale.  Re-sending startChoices
      // (debounced to ≤ 1 call per 600 ms) refreshes the table so the clicker
      // can answer immediately without appearing as IDLE.
      if (fun == "keyboardOnlineOne" || fun == "keypadOnlineOne") {
        auto infos          = j.value("infos", json::object());
        std::string sn      = infos.value("keySn", "");
        std::string key_id  = infos.value("keyId", "");
        std::string model   = infos.value("hModel", "");
        std::string version = infos.value("keyVer", "");
        Logger::hid()->info("[ClickerClient] {} SN={} Slot={} (pairing_active={})",
                            fun, sn, key_id, pairing_active_.load());

        if (pairing_active_) {
          // Normal pairing flow — forward to UI registration list.
          if (!sn.empty() && on_register_)
            on_register_(RegisteredClicker{sn, key_id, model, version});
        } else {
          // Answering-mode clicker appeared (late-join or sleep-wake).
          // Re-send startChoices on a detached thread (must not block the
          // receive callback) so WSCmdApp refreshes its slot table.
          Logger::hid()->info(
              "[ClickerClient] Clicker SN={} appeared in answering mode — "
              "scheduling debounced startChoices refresh", sn);
          std::thread([this] { sendStartAnsweringDebounced(); }).detach();
        }
        return;
      }

      // ── Pair-mode diagnostic ──────────────────────────────────────────
      if (fun == "pairMode") {
        std::string mode = j.value("infos", json::object()).value("value", "?");
        Logger::hid()->info("[ClickerClient] pairMode={} (4=QuickMatch 8=Whitelist)", mode);
        return;
      }

      // ── Click answer (normal answering mode) ──────────────────────────
      if (fun == "answerChoices" || fun == "answerNumber") {
        auto infos         = j.value("infos", json::object());
        std::string key_sn = infos.value("keySn", "");
        std::string kv_str = infos.value("keyValue", "");
        int val = 0;
        if (!kv_str.empty()) {
          if      (kv_str == "A") val = 1;
          else if (kv_str == "B") val = 2;
          else if (kv_str == "C") val = 3;
          else if (kv_str == "D") val = 4;
          else if (kv_str == "E") val = 5;
          else if (kv_str == "F") val = 6;
          else { try { val = std::stoi(kv_str); } catch (...) {} }
        }
        if (!key_sn.empty() && val != 0) {
          auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
          auto ev = ClickEvent::make(key_sn, val, static_cast<int64_t>(ts),
                                     NonceGenerator::generate());
          Logger::hid()->info("[ClickerClient] Click SN={} Val={}", key_sn, val);
          on_click_(std::move(ev)); // fires immediately — no queueing here
        }
        return;
      }

    } catch (const std::exception &e) {
      Logger::errors()->error("[ClickerClient] Parse error: {}", e.what());
    }
  }

  // ── Outbound commands ────────────────────────────────────────────────────

  void sendReadBase() {
    safeWsSend(json{{"fun", "readConnectBase"}, {"baseId", base_id_}}.dump());
  }

  void sendStartAnswering() {
    safeWsSend(json{{"fun", "startChoices"},
                    {"baseId", base_id_},
                    {"baseSn", base_sn_},
                    {"params",
                     {{"optionsMode", "1"}, // A–F letter display
                      {"secrecyMode", "0"}, // visible answer
                      {"modifyMode",  "1"}, // student can change answer
                      {"lessMode",    "2"}, // show last answer; never "Sent"
                      {"options",     "6"}, // A–F (6 choices)
                      {"optionalN",   "1"}}},
                    {"packetTag", "1"}}
                   .dump());
    // Record the send timestamp for debouncing.
    last_startchoices_ms_.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count(),
        std::memory_order_relaxed);
    Logger::hid()->info("[ClickerClient] startChoices sent (baseSn={})", base_sn_);
  }

  // Debounced variant — used when a late-joining or sleep-waking clicker
  // triggers a startChoices refresh.  Guards against a burst of
  // keyboardOnlineOne events (e.g. many clickers waking simultaneously)
  // from restarting the WSCmdApp question session in rapid succession.
  //
  // MUST be called from a dedicated thread, NOT from the WS receive callback.
  void sendStartAnsweringDebounced() {
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
    int64_t last = last_startchoices_ms_.load(std::memory_order_relaxed);
    if (now - last < kStartChoicesDebounceMs) {
      Logger::hid()->debug(
          "[ClickerClient] startChoices debounced ({}ms since last send)",
          now - last);
      return;
    }
    // Brief settle delay — give WSCmdApp a moment to finish registering the
    // newly appeared clicker before we refresh the session.
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    if (!pairing_active_.load())
      sendStartAnswering();
  }

  void safeWsSend(const std::string &msg) {
    if (ws_.getReadyState() == ix::ReadyState::Open)
      ws_.send(msg);
  }

  // ── Members ──────────────────────────────────────────────────────────────
  ix::WebSocket ws_;
  std::string   ws_url_;
  int           base_id_{1};
  std::string   base_sn_{};

  ClickCallback    on_click_;
  StatusCallback   on_status_;
  RegisterCallback on_register_;
  ClearCallback    on_clear_;

  std::atomic<bool>    running_{false};
  std::atomic<bool>    ws_connected_{false};
  std::atomic<bool>    base_station_active_{false};
  std::atomic<bool>    pairing_active_{false};

  // Timestamp (steady_clock ms) of the last startChoices send.
  // Used by sendStartAnsweringDebounced() to suppress rapid re-fires
  // when multiple clickers join or wake simultaneously.
  std::atomic<int64_t> last_startchoices_ms_{0};

  std::thread reconnect_thread_;
  std::thread heartbeat_thread_;
};

} // namespace svs
