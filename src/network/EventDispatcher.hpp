#pragma once
// =============================================================================
//  EventDispatcher.hpp
//  Dispatches click events to the REST API.
//
//  CONCURRENCY DESIGN
//  ──────────────────
//  The LockFreeQueue used here is SPSC (single-producer, single-consumer).
//  Having multiple threads call pop() concurrently is a data race — two threads
//  can simultaneously read the same tail position and both dequeue the same item,
//  causing duplicate API posts and undefined behaviour.
//
//  Fix: one dedicated drain thread owns all calls to queue_.pop().
//  For each valid event it spawns a detached thread that runs postWithRetry
//  independently, so HTTP calls run in parallel without blocking the drain loop.
//
//  Condition variable usage:
//    cv_.wait(lk, pred) — blocks until notifyPush() wakes it, with zero
//    polling delay.  (The old cv_.wait_for(5ms) added unnecessary latency.)
// =============================================================================
#include "app/Config.hpp"
#include "domain/ClickEvent.hpp"
#include "logging/LatencyLogger.hpp"
#include "logging/Logger.hpp"
#include "network/HttpClient.hpp"
#include "network/SessionManager.hpp"
#include "queue/LockFreeQueue.hpp"
#include "security/HmacSigner.hpp"
#include "storage/OfflineQueue.hpp"
#include "ui/UiState.hpp"
#include <atomic>
#include <nlohmann/json.hpp>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace svs {

using json = nlohmann::json;

/// Dispatches click events to the REST API.
///
/// Design principles:
///  - ONE drain thread owns queue_.pop() — SPSC correctness guarantee.
///  - Each valid event gets its own detached HTTP thread — true parallelism.
///  - notifyPush() wakes the drain thread with zero polling delay.
///  - Pre-validation events are discarded, never stored for replay.
///  - Retry with exponential back-off + jitter.
class EventDispatcher {
public:
  EventDispatcher(const Config &cfg, SessionManager &session_mgr,
                  LockFreeQueue<ClickEvent> &queue, OfflineQueue &offline,
                  HttpClient &http, UiState &ui_state)
      : session_mgr_(session_mgr), queue_(queue), offline_(offline),
        api_url_(cfg.require("API_BASE_URL") + cfg.require("API_POST_ENDPOINT")),
        max_retries_(cfg.getInt("API_MAX_RETRIES", 10)),
        retry_base_ms_(cfg.getInt("API_RETRY_BASE_MS", 30)),
        hmac_enabled_(cfg.getBool("HMAC_ENABLED", false)),
        hmac_signer_(cfg.getBool("HMAC_ENABLED", false)
                         ? std::make_unique<HmacSigner>(cfg.get("HMAC_SECRET_ENV_VAR", ""))
                         : nullptr),
        http_(http),
        ui_state_(ui_state) {}

  ~EventDispatcher() { stop(); }

  void start() {
    running_     = true;
    drain_       = std::thread(&EventDispatcher::drainLoop,  this);
    flush_checker_ = std::thread(&EventDispatcher::flushLoop, this);
  }

  void stop() {
    {
      std::lock_guard lk(cv_mu_);
      running_ = false;
    }
    cv_.notify_all();
    if (drain_.joinable())         drain_.join();
    if (flush_checker_.joinable()) flush_checker_.join();
  }

  /// Wake the drain thread immediately after a push.
  /// Called by the producer (clicker thread) — latency ≈ 0.
  void notifyPush() { cv_.notify_one(); }

  /// Reset session state so onSessionValidated() fires again on the next
  /// validated event. Call this BEFORE every SessionManager::validate() so
  /// the ring-buffer purge and generation bump happen on every VALIDATE
  /// press, not just the first one.
  void resetSession() {
    session_start_ms_.store(0, std::memory_order_relaxed);
    Logger::session()->info("[Dispatcher] Session reset — ready for re-validation");
  }

private:
  // ── Session-start helpers ─────────────────────────────────────────────────

  /// Drains and discards ALL pending events from the ring buffer.
  ///
  /// Called at session validation to purge any clicks that accumulated
  /// before the session was valid.  This eliminates the race window
  /// (~0–100 ms) where ev.timestamp_ms ≥ session_start_ms_ could let a
  /// pre-quiz event slip through the timestamp gate in drainLoop().
  void purgeQueue() {
    size_t n = 0;
    while (queue_.pop()) ++n;
    if (n > 0)
      Logger::api()->info(
          "[Dispatcher] Purged {} pre-session events from ring buffer", n);
  }

  /// Called the first time a validated event arrives (or on re-validation).
  /// Records the session-start epoch, bumps the generation counter so any
  /// in-flight retry threads from a prior session abort their next attempt,
  /// purges the ring buffer, and clears the encrypted offline queue.
  void onSessionValidated() {
    if (session_start_ms_.load(std::memory_order_relaxed) != 0)
      return;

    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    int64_t expected = 0;
    if (!session_start_ms_.compare_exchange_strong(expected, now,
                                                   std::memory_order_relaxed))
      return; // another thread already handled this

    // Increment generation BEFORE purging so any retry threads that check
    // generation_ after this point will immediately abort.
    generation_.fetch_add(1, std::memory_order_acq_rel);

    purgeQueue();
    offline_.clear();

    Logger::session()->info(
        "[Dispatcher] Session validated (gen={}) — ring buffer purged, "
        "pre-session offline events cleared",
        generation_.load(std::memory_order_relaxed));
  }

  // ── Drain loop ──────────────────────────────────────────────────────────────
  // THE ONLY THREAD that calls queue_.pop(). SPSC contract.
  void drainLoop() {
    Logger::api()->info("[Dispatcher] Drain thread started");

    while (true) {
      // Sleep until the producer calls notifyPush() or stop() is called.
      // cv_.wait(pred) — zero polling delay, woken instantly by notify_one().
      {
        std::unique_lock lk(cv_mu_);
        cv_.wait(lk, [this] { return !queue_.empty() || !running_; });
      }
      if (!running_ && queue_.empty())
        break;

      // Drain all available items without re-sleeping.
      while (auto ev_opt = queue_.pop()) {
        ClickEvent ev = std::move(*ev_opt);

        // Gate: discard events before session validation.
        if (!session_mgr_.isValid()) {
          Logger::session()->debug(
              "[Dispatcher] Session not validated — discarding sn={}", ev.serial_no);
          continue;
        }

        // First validated event: bump generation, purge ring buffer, clear
        // offline queue.  This must happen before we dispatch to ensure no
        // stale pre-session events can slip through the timestamp gate.
        onSessionValidated();

        // Capture the current session generation BEFORE spawning the thread.
        // The retry thread will abort if generation_ changes (re-validation).
        uint64_t gen = generation_.load(std::memory_order_relaxed);

        // Drop events whose timestamp precedes session validation.
        int64_t session_t = session_start_ms_.load(std::memory_order_relaxed);
        if (session_t > 0 && ev.timestamp_ms < session_t) {
          Logger::api()->warn(
              "[Dispatcher] Dropping pre-session event sn={} ts={} (session={})",
              ev.serial_no, ev.timestamp_ms, session_t);
          continue;
        }

        // Dispatch HTTP on its own thread — drain loop never blocks on network.
        // Each thread has a thread-local curl handle (keep-alive reused).
        // The generation stamp lets the thread self-abort if the session
        // changes while it is sleeping through an exponential back-off retry.
        std::thread([this, ev = std::move(ev), gen] {
          postWithRetry(ev, gen);
        }).detach();
      }
    }

    Logger::api()->info("[Dispatcher] Drain thread stopped");
  }

  // ── Offline flush loop ─────────────────────────────────────────────────────
  void flushLoop() {
    while (running_) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      if (!session_mgr_.isValid() || !offline_.hasEvents())
        continue;

      Logger::api()->info("[Dispatcher] Flushing offline queue...");
      int64_t  session_t = session_start_ms_.load(std::memory_order_relaxed);
      uint64_t gen       = generation_.load(std::memory_order_relaxed);
      offline_.flush([this, session_t, gen](ClickEvent ev) {
        if (session_t > 0 && ev.timestamp_ms < session_t) {
          Logger::api()->warn("[Dispatcher] FlushLoop: skipping stale sn={}", ev.serial_no);
          return;
        }
        postWithRetry(ev, gen);
      });
    }
  }

  // ── HTTP post with exponential back-off ──────────────────────────────────
  //
  // @param gen  Session generation captured at dispatch time.  If generation_
  //             has advanced beyond this value when a retry is about to fire,
  //             the function aborts immediately.  This prevents in-flight retry
  //             threads from a previous quiz question posting the wrong value
  //             over a correct answer in the current quiz question.
  void postWithRetry(const ClickEvent &ev, uint64_t gen) {
    auto info = session_mgr_.getInfo();
    if (!info.valid) {
      offline_.enqueue(ev);
      return;
    }

    json body = {{"sessionId", info.session_id},
                 {"deviceID",  ev.device_sn},
                 {"eventNum",  ev.value}};

    auto t0 = std::chrono::steady_clock::now();

    // Jitter: ±20% of base delay prevents thunderstorm retries from many
    // classrooms all failing and retrying at exactly the same interval.
    thread_local std::mt19937 rng{std::random_device{}()};
    long delay_ms = retry_base_ms_;

    for (int attempt = 0; attempt <= max_retries_; ++attempt) {
      // ─ Generation check ────────────────────────────────────────────
      // If the session has been re-validated (e.g. operator clicked VALIDATE
      // again mid-quiz), our generation stamp is stale.  The new session's
      // drain loop is already dispatching the correct events, so we must not
      // let this retry overwrite them with the old value.
      if (generation_.load(std::memory_order_relaxed) != gen) {
        Logger::api()->warn(
            "[API] Aborting retry sn={} — session generation changed "
            "(was gen={}, now gen={})",
            ev.serial_no, gen,
            generation_.load(std::memory_order_relaxed));
        return;
      }

      try {
        auto resp = http_.post(api_url_, body);

        auto t1         = std::chrono::steady_clock::now();
        int64_t latency = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        if (resp.ok()) {
          auto api_done_ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
          Logger::perf()->info("[API] sn={} device={} value={} → HTTP {} latency={}ms",
                               ev.serial_no, ev.device_sn, ev.value,
                               resp.status_code, latency);
          ui_state_.updateEventLatency(ev.serial_no,
                                       static_cast<int>(resp.status_code),
                                       latency);
          latencyLog().recordApiDone(ev.serial_no,
                                     static_cast<int>(resp.status_code),
                                     api_done_ts);
          return; // success
        }

        Logger::api()->warn("[API] HTTP {} for sn={} attempt={}/{}",
                            resp.status_code, ev.serial_no, attempt + 1, max_retries_);
      } catch (const std::exception &e) {
        Logger::errors()->warn("[API] Exception sn={} attempt={}: {}",
                               ev.serial_no, attempt + 1, e.what());
      }

      if (attempt < max_retries_) {
        std::uniform_int_distribution<long> jitter(-(delay_ms / 5), delay_ms / 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms + jitter(rng)));
        delay_ms = std::min(delay_ms * 2, 30000L);
      }
    }

    Logger::errors()->error("[API] sn={} failed after {} retries, persisting offline",
                            ev.serial_no, max_retries_);
    offline_.enqueue(ev);
  }

  // ── Members ──────────────────────────────────────────────────────────────
  SessionManager              &session_mgr_;
  LockFreeQueue<ClickEvent>   &queue_;
  OfflineQueue                &offline_;
  std::string                  api_url_;
  int                          max_retries_;
  long                         retry_base_ms_;
  bool                         hmac_enabled_;
  std::unique_ptr<HmacSigner>  hmac_signer_;
  HttpClient                  &http_;
  UiState                     &ui_state_;

  std::atomic<bool>    running_{false};
  std::atomic<int64_t> session_start_ms_{0};

  // Session generation counter.  Incremented every time onSessionValidated()
  // fires (i.e. on every VALIDATE action).  Each dispatched postWithRetry
  // thread captures the generation at launch time and aborts if it has
  // advanced — preventing stale pre-quiz values from overwriting correct
  // current-quiz answers on the server.
  std::atomic<uint64_t> generation_{0};

  // Condition variable: drain thread sleeps here, woken instantly by notifyPush().
  std::mutex              cv_mu_;
  std::condition_variable cv_;

  std::thread drain_;         // sole consumer of queue_ (SPSC)
  std::thread flush_checker_; // periodically replays offline events
};

} // namespace svs
