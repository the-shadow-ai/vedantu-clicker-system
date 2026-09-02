#pragma once
// =============================================================================
//  LatencyLogger — per-stage CSV latency tracker (immediate write per stage)
//
//  Writes one CSV row IMMEDIATELY for every stage of each click event:
//    ENTRY   : USB dongle packet received — stamped in main.cpp
//    UI      : Row first rendered on screen — stamped in MainWindow
//    API     : HTTP POST response received — stamped in EventDispatcher
//
//  Each stage creates its own row instantly so data is never lost,
//  even if the app exits or the session is not yet validated.
//
//  CSV file: <exe_dir>\logs\latency_YYYY-MM-DD.csv  (opens in Excel)
//  A new file is created automatically each day.
//
//  THREAD-SAFETY:
//    All public methods are thread-safe via an internal mutex.
//    Do NOT call recordUiRender while holding UiState::rows_mu.
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace svs {

class LatencyLogger {
public:
    LatencyLogger() = default;
    ~LatencyLogger() { shutdown(); }

    // -------------------------------------------------------------------------
    //  Open CSV. Call once at startup from main().
    // -------------------------------------------------------------------------
    void open(const std::string& exe_dir) {
        std::lock_guard<std::mutex> lk(mu_);
        exe_dir_ = exe_dir;
        rotateCsv();
    }

    // -------------------------------------------------------------------------
    //  Stage 1 — Dongle packet received.  Writes a row IMMEDIATELY.
    //  Call from ClickerClient / main.cpp thread.
    // -------------------------------------------------------------------------
    void recordEntry(uint64_t serial, const std::string& device_id,
                     int value, int64_t entry_ts_ms) {
        std::lock_guard<std::mutex> lk(mu_);
        // Cache for delta calculations in later stages
        entry_times_[serial] = entry_ts_ms;
        writeRow(serial, "1_ENTRY", device_id, value,
                 entry_ts_ms, 0 /*delta from entry*/);
    }

    // -------------------------------------------------------------------------
    //  Stage 2 — Row first painted on screen.  Writes a row IMMEDIATELY.
    //  MUST NOT be called while holding UiState::rows_mu.
    //  Pass the pre-computed render_ts_ms to avoid calling nowMs() under lock.
    // -------------------------------------------------------------------------
    void recordUiRender(uint64_t serial, int64_t render_ts_ms) {
        std::lock_guard<std::mutex> lk(mu_);
        int64_t entry = entryTs(serial);
        int delta = (entry > 0) ? static_cast<int>(render_ts_ms - entry) : -1;
        writeRow(serial, "2_UI_PAINT", "", 0, render_ts_ms, delta);
    }

    // -------------------------------------------------------------------------
    //  Stage 3 — API HTTP response received.  Writes a row IMMEDIATELY.
    //  Call from EventDispatcher worker thread.
    // -------------------------------------------------------------------------
    void recordApiDone(uint64_t serial, int http_status, int64_t api_done_ts_ms) {
        std::lock_guard<std::mutex> lk(mu_);
        int64_t entry = entryTs(serial);
        int delta = (entry > 0) ? static_cast<int>(api_done_ts_ms - entry) : -1;
        writeRowApi(serial, api_done_ts_ms, delta, http_status);
        entry_times_.erase(serial);  // cleanup
    }

    // -------------------------------------------------------------------------
    //  Flush + close.  Call at shutdown.
    // -------------------------------------------------------------------------
    void shutdown() {
        std::lock_guard<std::mutex> lk(mu_);
        if (file_.is_open()) { file_.flush(); file_.close(); }
    }

private:
    // ── Helpers ──────────────────────────────────────────────────────────────
    int64_t entryTs(uint64_t serial) const {
        auto it = entry_times_.find(serial);
        return (it != entry_times_.end()) ? it->second : 0;
    }

    // Write a ENTRY or UI row  (device/value blank for UI stage)
    void writeRow(uint64_t serial, const char* stage,
                  const std::string& device, int value,
                  int64_t ts_ms, int delta_ms) {
        if (!file_.is_open()) return;
        if (currentDateStr() != open_date_) rotateCsv();

        file_ << serial
              << "," << stage
              << "," << device
              << "," << (value > 0 ? std::to_string(value) : "")
              << "," << fmtMs(ts_ms)
              << "," << ts_ms
              << "," << (delta_ms >= 0 ? std::to_string(delta_ms) : "")
              << ","   // http_status blank for non-API stages
              << "\n";
        // No flush here — OS buffers writes and flushes on close().
        // Per-write flush() issued a FlushFileBuffers kernel call (~2 ms each)
        // serialising all click events through the mutex for no benefit.
    }

    // Write an API_DONE row that includes http_status
    void writeRowApi(uint64_t serial, int64_t ts_ms, int delta_ms, int http_status) {
        if (!file_.is_open()) return;
        if (currentDateStr() != open_date_) rotateCsv();

        file_ << serial
              << ",3_API_DONE"
              << ","    // device (blank)
              << ","    // value (blank)
              << "," << fmtMs(ts_ms)
              << "," << ts_ms
              << "," << (delta_ms >= 0 ? std::to_string(delta_ms) : "")
              << "," << http_status
              << "\n";
        // No per-write flush — see writeRow() comment above.
    }

    // ── CSV file management ───────────────────────────────────────────────────
    void rotateCsv() {
        if (file_.is_open()) file_.close();
        open_date_ = currentDateStr();

        std::string logs_dir = exe_dir_ + "\\logs";
        std::filesystem::create_directories(logs_dir);
        std::string path = logs_dir + "\\latency_" + open_date_ + ".csv";

        bool needs_header = !std::filesystem::exists(path);
        file_.open(path, std::ios::app);
        if (needs_header && file_.is_open()) {
            file_
                << "Serial No,"
                   "Stage,"
                   "Device ID,"
                   "Click Value,"
                   "Wall Time (HH:MM:SS.mmm),"
                   "Epoch ms,"
                   "Delta from Entry ms,"
                   "HTTP Status"
                << "\n";
            file_.flush(); // flush the header so it appears even if app exits early
        }
    }

    // ── Time helpers ─────────────────────────────────────────────────────────
    static std::string fmtMs(int64_t ms) {
        if (ms <= 0) return "";
        std::time_t secs = ms / 1000;
        std::tm tm{};
        localtime_s(&tm, &secs);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03lld",
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            static_cast<long long>(ms % 1000));
        return buf;
    }

    static std::string currentDateStr() {
        auto now  = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &t);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        return buf;
    }

    // ── Members ───────────────────────────────────────────────────────────────
    std::mutex mu_;
    std::ofstream file_;
    std::string exe_dir_;
    std::string open_date_;
    std::unordered_map<uint64_t, int64_t> entry_times_;  // serial → entry_ts_ms
};

// Singleton accessor
inline LatencyLogger& latencyLog() {
    static LatencyLogger inst;
    return inst;
}

} // namespace svs
