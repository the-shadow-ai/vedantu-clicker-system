#pragma once
#include <atomic>
#include <cstdint>
#include <string>


namespace svs {

/// A single click event captured from the SunVote hardware.
struct ClickEvent {
  uint64_t serial_no;    ///< Global monotonic counter (auto-assigned)
  std::string device_sn; ///< Clicker serial number (keySn from WSCmdApp JSON)
  int value;             ///< Button pressed: 1–13
  int64_t timestamp_ms;  ///< Unix epoch milliseconds (UTC)
  std::string nonce;     ///< UUID v4 for replay-attack prevention

  /// Factory: create with auto-incremented serial number
  static ClickEvent make(std::string device_sn, int value, int64_t ts_ms,
                         std::string nonce);

private:
  static std::atomic<uint64_t> s_counter;
};

} // namespace svs
