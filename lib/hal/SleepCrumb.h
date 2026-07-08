#pragma once

#include <cstdint>

// Breadcrumb trail for diagnosing hangs during deep-sleep entry: the device
// drains battery behind a frozen sleep screen and only recovers via the reset
// button (upstream issue #1396). Each stage of the sleep path appends a line to
// a small trail file on the SD card as it executes. SD is used instead of
// RTC_NOINIT memory because the reset button toggles CHIP_PU and can wipe the
// RTC domain — the SD trail survives every recovery path, including full
// power-off.
//
// On boot, reportOnBoot() inspects the trail left by the previous session:
// a trail that reached FINAL_TEARDOWN was a successful sleep and is deleted
// silently; anything else is converted into a human-readable entry appended to
// /sleep_debug.txt, which the tester can copy off the card and share.
namespace SleepCrumb {

enum Stage : uint8_t {
  ENTER = 1,                   // enterDeepSleep() started (written by begin())
  STATE_SAVED = 2,             // APP_STATE persisted
  RENDER_LOCK_ACQUIRED = 3,    // activity replacement acquired the RenderLock
  OLD_ACTIVITY_EXITED = 4,     // outgoing activity onExit() finished
  SCREEN_RENDER_START = 5,     // SleepActivity::onEnter() started
  SCREEN_RENDER_DONE = 6,      // sleep screen displayed
  ACTIVITY_TEARDOWN_DONE = 7,  // goToSleep() returned to enterDeepSleep()
  FRAME_SAVED = 8,             // quick-resume framebuffer written (quick-resume sleeps only)
  WIFI_DOWN = 9,               // WiFi teardown point passed ("WIFI_OFF" collides with an Arduino macro)
  DISPLAY_SLEEP = 10,          // panel put into deep sleep
  POWER_MGR_ENTER = 11,        // HalPowerManager::startDeepSleep() entered
  FINAL_TEARDOWN = 12,         // power button released; battery latch teardown is next (= success)
};

// Arm the trail and write the header + ENTER stage. `trigger` must be a static
// string ("button", "timeout", "wake-verify", "usb-power").
void begin(const char* trigger, bool fromReader, uint8_t sleepScreenMode, uint16_t batteryPercent);

// Append a stage to the trail. No-op unless begin() ran this boot, so marks in
// shared code paths (ActivityManager) stay silent outside the sleep sequence.
void mark(Stage stage);

// Call once at boot after storage is up: report an incomplete trail from the
// previous session to /sleep_debug.txt and clear the trail file.
void reportOnBoot();

}  // namespace SleepCrumb
