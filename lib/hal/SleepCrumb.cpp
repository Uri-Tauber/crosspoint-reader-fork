#include "SleepCrumb.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <cstring>

#ifndef CROSSPOINT_VERSION
#define CROSSPOINT_VERSION "unknown"
#endif

namespace {

constexpr char TRAIL_PATH[] = "/.crosspoint/sleep_trail.txt";
constexpr char REPORT_PATH[] = "/sleep_debug.txt";
// Stop appending once the report file is large, so a repeatedly failing device
// can't fill the card.
constexpr size_t REPORT_MAX_SIZE = 64 * 1024;
// A full trail is ~500 bytes; 1KB leaves headroom. Heap (not stack): exceeds
// the 256-byte local-variable budget, and it's a once-per-boot allocation.
constexpr size_t TRAIL_MAX_READ = 1024;

bool armed = false;

const char* stageName(const uint8_t stage) {
  switch (stage) {
    case SleepCrumb::ENTER:
      return "ENTER";
    case SleepCrumb::STATE_SAVED:
      return "STATE_SAVED";
    case SleepCrumb::RENDER_LOCK_ACQUIRED:
      return "RENDER_LOCK_ACQUIRED";
    case SleepCrumb::OLD_ACTIVITY_EXITED:
      return "OLD_ACTIVITY_EXITED";
    case SleepCrumb::SCREEN_RENDER_START:
      return "SCREEN_RENDER_START";
    case SleepCrumb::SCREEN_RENDER_DONE:
      return "SCREEN_RENDER_DONE";
    case SleepCrumb::ACTIVITY_TEARDOWN_DONE:
      return "ACTIVITY_TEARDOWN_DONE";
    case SleepCrumb::FRAME_SAVED:
      return "FRAME_SAVED";
    case SleepCrumb::WIFI_DOWN:
      return "WIFI_DOWN";
    case SleepCrumb::DISPLAY_SLEEP:
      return "DISPLAY_SLEEP";
    case SleepCrumb::POWER_MGR_ENTER:
      return "POWER_MGR_ENTER";
    case SleepCrumb::FINAL_TEARDOWN:
      return "FINAL_TEARDOWN";
    default:
      return "UNKNOWN";
  }
}

const char* resetReasonName(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "int-wdt";
    case ESP_RST_TASK_WDT:
      return "task-wdt";
    case ESP_RST_WDT:
      return "other-wdt";
    case ESP_RST_DEEPSLEEP:
      return "deepsleep-wake";
    case ESP_RST_BROWNOUT:
      return "brownout";
    default:
      return "unknown";
  }
}

// Open-append-close per line so every stage hits the card before the next step
// of the sleep path runs — a later hang must not lose earlier breadcrumbs.
void appendTrailLine(const char* line) {
  HalFile file = Storage.open(TRAIL_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("SLPC", "Trail open failed");
    return;
  }
  file.write(line, strlen(line));
}

}  // namespace

namespace SleepCrumb {

void begin(const char* trigger, const bool fromReader, const uint8_t sleepScreenMode, const uint16_t batteryPercent) {
  Storage.ensureDirectoryExists("/.crosspoint");
  Storage.remove(TRAIL_PATH);  // fresh trail per sleep attempt
  armed = true;
  char buf[192];
  snprintf(buf, sizeof(buf),
           "CrossPoint sleep trail v1\n"
           "fw=%s trigger=%s from-reader=%d sleep-screen=%u battery=%u%%\n",
           CROSSPOINT_VERSION, trigger, fromReader ? 1 : 0, sleepScreenMode, batteryPercent);
  appendTrailLine(buf);
  mark(ENTER);
}

void mark(const Stage stage) {
  if (!armed) {
    return;
  }
  char buf[64];
  snprintf(buf, sizeof(buf), "stage=%02u %s t=%lu\n", static_cast<unsigned>(stage), stageName(stage), millis());
  appendTrailLine(buf);
  LOG_DBG("SLPC", "stage %u %s", static_cast<unsigned>(stage), stageName(stage));
}

void reportOnBoot() {
  if (!Storage.exists(TRAIL_PATH)) {
    return;
  }

  auto trail = makeUniqueNoThrow<char[]>(TRAIL_MAX_READ);
  if (!trail) {
    LOG_ERR("SLPC", "OOM: %u bytes", static_cast<unsigned>(TRAIL_MAX_READ));
    return;
  }
  const size_t len = Storage.readFileToBuffer(TRAIL_PATH, trail.get(), TRAIL_MAX_READ);
  Storage.remove(TRAIL_PATH);
  if (len == 0) {
    return;
  }

  // A trail that reached FINAL_TEARDOWN was a successful sleep — discard.
  if (strstr(trail.get(), "FINAL_TEARDOWN") != nullptr) {
    LOG_DBG("SLPC", "Previous sleep completed normally");
    return;
  }

  LOG_INF("SLPC", "Incomplete sleep trail found, appending report to %s", REPORT_PATH);

  HalFile report = Storage.open(REPORT_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!report) {
    LOG_ERR("SLPC", "Report open failed");
    return;
  }
  if (report.fileSize() > REPORT_MAX_SIZE) {
    LOG_ERR("SLPC", "Report file full, not appending");
    return;
  }

  const esp_reset_reason_t resetReason = esp_reset_reason();
  char buf[224];
  snprintf(buf, sizeof(buf),
           "==== FAILED SLEEP DETECTED ====\n"
           "The firmware hung while entering standby; the trail below shows\n"
           "the last stage it reached before freezing.\n"
           "this boot: fw=%s reset-reason=%d(%s) wakeup-cause=%d\n"
           "---- trail of the session that hung ----\n",
           CROSSPOINT_VERSION, static_cast<int>(resetReason), resetReasonName(resetReason),
           static_cast<int>(esp_sleep_get_wakeup_cause()));
  report.write(buf, strlen(buf));
  report.write(trail.get(), len);
  constexpr char FOOTER[] = "===============================\n\n";
  report.write(FOOTER, strlen(FOOTER));
}

}  // namespace SleepCrumb
