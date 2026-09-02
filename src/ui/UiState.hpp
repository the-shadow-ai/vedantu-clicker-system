#pragma once
#define WIN32_LEAN_AND_MEAN
#include "domain/ClickEvent.hpp"
#include "hid/RegisteredClicker.hpp" // No longer pulls in ClickerClient.hpp
#include "network/SessionManager.hpp"
#include <atomic>
#include <chrono>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace svs {

/// Shared UI state. Updated by multiple threads, read by the render thread.
/// Uses fine-grained locking: each protected group has its own mutex.
struct UiState {
  // --- UI Thread Wakeup Handle
  std::atomic_uintptr_t hwnd_to_wake{0};

  // --- Dongle
  std::atomic<bool> dongle_connected{false};

  // --- Session
  std::atomic<bool> session_valid{false};
  // Set to true when validation fails; cleared on success.
  // Read by the UI render thread to show an inline error banner
  // instead of a blocking MessageBoxA popup.
  std::atomic<bool> validation_error{false};
  std::string session_title;
  std::string session_presenter;
  std::string session_start;
  std::string session_end;
  mutable std::mutex session_mu;

  // --- Event log (newest first)
  struct Row {
    uint64_t serial_no;
    std::string device_id;
    int value;
    std::string timestamp;
    int64_t entry_ts_ms{0};
    int latency_ms{-1};
    int ui_delay_ms{-1};
    int api_status{0};
  };
  std::deque<Row> rows; // rows[0] = newest
  mutable std::mutex rows_mu;
  static constexpr size_t MAX_ROWS = 10000; // increased from 1000

  // O(1) serial-number → row lookup for API latency updates.
  // Maintained under the same rows_mu as rows — no extra lock needed.
  std::unordered_map<uint64_t, size_t> serial_index_; // serial_no → rows[i]

  // --- Clicker pairing (startQuickMatch session)
  std::atomic<bool> pairing_active{false};
  std::vector<RegisteredClicker> registered_clickers;
  mutable std::mutex reg_mu;

  void addEvent(const ClickEvent &ev) {
    auto ms = ev.timestamp_ms;
    std::time_t secs = ms / 1000;
    std::tm tm_info{};
    localtime_s(&tm_info, &secs);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03lld", tm_info.tm_hour,
                  tm_info.tm_min, tm_info.tm_sec, ms % 1000);
    Row r{ev.serial_no, ev.device_sn, ev.value, buf, ev.timestamp_ms, -1, -1, 0};
    {
      std::lock_guard lk(rows_mu);
      // Shift all indices up by 1 since we're inserting at front
      // Only rebuild index if it's already populated (avoid cold-start O(n))
      if (!serial_index_.empty()) {
        std::unordered_map<uint64_t, size_t> new_idx;
        new_idx.reserve(serial_index_.size() + 1);
        for (auto& [sn, idx] : serial_index_)
          new_idx[sn] = idx + 1;
        serial_index_ = std::move(new_idx);
      }
      serial_index_[ev.serial_no] = 0;
      rows.push_front(std::move(r));
      if (rows.size() > MAX_ROWS) {
        // Remove evicted tail entry from index
        serial_index_.erase(rows.back().serial_no);
        rows.pop_back();
      }
    }
    wakeRender();
  }

  void updateEventLatency(uint64_t serial_no, int status, int latency_ms) {
    {
      std::lock_guard lk(rows_mu);
      // O(1) lookup via serial_index_ instead of linear scan across 10000 rows
      auto it = serial_index_.find(serial_no);
      if (it != serial_index_.end()) {
        auto& r = rows[it->second];
        r.api_status = status;
        r.latency_ms = latency_ms;
      }
    }
    wakeRender();
  }

  void addRegisteredClicker(RegisteredClicker rc) {
    {
      std::lock_guard lk(reg_mu);
      for (auto &existing : registered_clickers)
        if (existing.sn == rc.sn)
          return;
      registered_clickers.push_back(std::move(rc));
    }
    wakeRender();
  }

  void clearRegistered() {
    {
      std::lock_guard lk(reg_mu);
      registered_clickers.clear();
    }
    wakeRender();
  }

  void updateSession(const SessionInfo &si) {
    auto fmtTime = [](int64_t ms) -> std::string {
      if (ms == 0)
        return "—";
      std::time_t t = ms / 1000;
      char buf[32]{"Invalid Time"};
      std::tm tm{};
      if (localtime_s(&tm, &t) == 0)
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
      return buf;
    };
    {
      std::lock_guard lk(session_mu);
      session_title = si.title;
      session_presenter = si.presenter;
      session_start = fmtTime(si.start_time_ms);
      session_end = fmtTime(si.end_time_ms);
      session_valid = si.valid;
    }
    wakeRender();
  }

private:
  /// Wake the render thread instantly from its MsgWait sleep.
  void wakeRender() const {
    HWND h = reinterpret_cast<HWND>(hwnd_to_wake.load(std::memory_order_relaxed));
    if (h)
      ::PostMessageA(h, WM_APP + 1, 0, 0);
  }
};

} // namespace svs
