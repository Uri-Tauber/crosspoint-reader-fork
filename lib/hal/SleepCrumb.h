#pragma once

#include <cstdint>

// SD-backed forensic log for the sleep/wake path.
//
// Appends a human-readable record to /sleep_debug.txt that a field tester can
// copy off the card. It does not just trace stages — on every boot it renders a
// verdict on the previous sleep, because the interesting question cannot be
// answered from inside the sleep itself.
//
// Why a verdict is necessary: the battery latch (GPIO13) cuts all power on
// battery, so a successful sleep leaves no code running to report success, and
// e-ink retains its image with no power at all. A device that hung with the
// sleep screen on the panel is visually identical to one that slept. The two
// are told apart on the *next* boot by correlating the stage the sleep reached
// with this boot's reset reason and the battery delta across the gap: a real
// sleep costs ~0% battery, a hang costs the awake rate.
//
// Nothing is ever deleted. A wake -> boot -> re-sleep loop is a sequence of
// individually successful sleeps, so discarding successes would hide exactly
// the failure mode that matters.
namespace SleepCrumb {

// Stages of the sleep-entry path in execution order. Values are persisted in
// the state file: append new stages at the end, never renumber.
enum Stage : uint8_t {
  STAGE_NONE = 0,
  SLEEP_REQUEST = 1,
  STATE_SAVED = 2,
  ACT_EXIT_START = 3,
  ACT_EXIT_DONE = 4,
  SCREEN_RENDER_START = 5,
  SCREEN_RENDER_DONE = 6,
  GOTO_SLEEP_DONE = 7,
  FRAME_SAVED = 8,
  WIFI_DOWN = 9,  // not WIFI_OFF: that collides with an Arduino macro
  TILT_SLEEP = 10,
  DISPLAY_SLEEP = 11,
  PM_ENTER = 12,
  BTN_RELEASED = 13,
  SERIAL_DOWN = 14,
  PRE_LATCH = 15,       // last durable record; the latch opens immediately after
  SLEEP_RETURNED = 16,  // esp_deep_sleep_start() returned — must never happen
};

// Call once per boot, as early as possible after Storage.begin(). Appends the
// boot record and the verdict on the previous sleep. The battery reading is
// passed in rather than sampled here so this cannot depend on HalPowerManager
// having been initialised first.
void logBoot(uint16_t batteryPercent);

// Records the outcome of the power-button wake check.
void logWakeVerify(bool passed, unsigned long heldMs, uint16_t requiredMs);

// Opens a sleep record. `trigger` is why we are sleeping ("button", "timeout",
// "wake-verify", "usb-power"); `activityName` is the activity slept from, which
// is what distinguishes a reader sleep from a menu sleep.
void beginSleep(const char* trigger, const char* activityName, bool fromReader, uint8_t sleepScreenMode,
                uint16_t batteryPercent);

// Records reaching `stage`. No-op unless a sleep record is open, so marks on
// shared paths stay silent during normal activity switches.
void mark(Stage stage);

}  // namespace SleepCrumb
