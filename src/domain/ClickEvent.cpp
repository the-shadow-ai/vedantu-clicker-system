#include "domain/ClickEvent.hpp"
#include <atomic>

namespace svs {

std::atomic<uint64_t> ClickEvent::s_counter{0};

ClickEvent ClickEvent::make(std::string device_sn, int value, int64_t ts_ms,
                            std::string nonce) {
  ClickEvent ev;
  ev.serial_no = ++s_counter;
  ev.device_sn = std::move(device_sn);
  ev.value = value;
  ev.timestamp_ms = ts_ms;
  ev.nonce = std::move(nonce);
  return ev;
}

} // namespace svs
