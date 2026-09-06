#pragma once
#include <cstdint>
inline uint32_t millis() {
  static uint32_t clock = 0;
  return ++clock;
}
inline void delay(unsigned) {}

struct FakeEsp {
  uint32_t getMinFreeHeap() { return 100000; }
};
inline FakeEsp ESP;
