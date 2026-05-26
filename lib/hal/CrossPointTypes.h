#pragma once

#include <cstdint>

namespace CrossPointOrientation {
enum Value : uint8_t {
  PORTRAIT = 0,
  LANDSCAPE_CW = 1,
  INVERTED = 2,
  LANDSCAPE_CCW = 3,
  COUNT
};
}

namespace CrossPointTiltPageTurn {
enum Value : uint8_t {
  TILT_OFF = 0,
  TILT_NORMAL = 1,
  TILT_INVERTED = 2,
  COUNT
};
}
