#pragma once
#include <string>

namespace svs {

/// Info about a clicker that successfully paired with the dongle base station.
/// Populated from `keyboardOnlineOne` events.
/// Defined as its own header to prevent circular includes between
/// ClickerClient.hpp and UiState.hpp.
struct RegisteredClicker {
  std::string sn;      ///< Hardware serial number (keySn) — permanent HW ID
  std::string key_id;  ///< Logical keypad slot assigned by dongle (e.g. "00001")
  std::string model;   ///< Hardware model code (hModel)
  std::string version; ///< Clicker firmware version (keyVer)
};

} // namespace svs
