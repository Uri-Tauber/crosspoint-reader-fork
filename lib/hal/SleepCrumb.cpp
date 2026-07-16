#include "SleepCrumb.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>

#ifndef CROSSPOINT_VERSION
#define CROSSPOINT_VERSION "unknown"
#endif

namespace {

constexpr char LOG_PATH[] = "/sleep_debug.txt";
constexpr char STATE_DIR[] = "/.crosspoint";
constexpr char STATE_PATH[] = "/.crosspoint/sleep_state.bin";

// Stop appending once the log is large so a repeatedly failing device cannot
// fill the card. 128KB is on the order of a thousand boots.
constexpr size_t LOG_MAX_SIZE = 128 * 1024;

constexpr uint32_t STATE_MAGIC = 0x534C5034;  // "SLP4"

// A genuine power-down costs no battery. Anything above this across a sleep
// means the device was drawing current while it was supposed to be off.
constexpr uint16_t SUSPICIOUS_DRAIN_PCT = 3;

// Persisted across the sleep so the next boot can judge what happened. Plain
// struct, no packing: read/written whole via a properly aligned local, so the
// RISC-V unaligned-access hazard does not apply.
struct State {
  uint32_t magic;
  uint32_t bootCount;
  uint32_t sleepSeq;
  uint16_t batteryAtSleep;
  uint8_t lastStage;
  uint8_t fromReader;
  char trigger[12];
  char activity[20];
};

State state{};
bool armed = false;
bool logFull = false;

const char* stageName(const uint8_t stage) {
  switch (stage) {
    case SleepCrumb::SLEEP_REQUEST:
      return "SLEEP_REQUEST";
    case SleepCrumb::STATE_SAVED:
      return "STATE_SAVED";
    case SleepCrumb::ACT_EXIT_START:
      return "ACT_EXIT_START";
    case SleepCrumb::ACT_EXIT_DONE:
      return "ACT_EXIT_DONE";
    case SleepCrumb::SCREEN_RENDER_START:
      return "SCREEN_RENDER_START";
    case SleepCrumb::SCREEN_RENDER_DONE:
      return "SCREEN_RENDER_DONE";
    case SleepCrumb::GOTO_SLEEP_DONE:
      return "GOTO_SLEEP_DONE";
    case SleepCrumb::FRAME_SAVED:
      return "FRAME_SAVED";
    case SleepCrumb::WIFI_DOWN:
      return "WIFI_DOWN";
    case SleepCrumb::TILT_SLEEP:
      return "TILT_SLEEP";
    case SleepCrumb::DISPLAY_SLEEP:
      return "DISPLAY_SLEEP";
    case SleepCrumb::PM_ENTER:
      return "PM_ENTER";
    case SleepCrumb::BTN_RELEASED:
      return "BTN_RELEASED";
    case SleepCrumb::SERIAL_DOWN:
      return "SERIAL_DOWN";
    case SleepCrumb::PRE_LATCH:
      return "PRE_LATCH";
    case SleepCrumb::SLEEP_RETURNED:
      return "SLEEP_RETURNED";
    default:
      return "NONE";
  }
}

const char* resetReasonName(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "UNKNOWN";
  }
}

const char* wakeupCauseName(const esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "UNDEFINED";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "TIMER";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "GPIO";
    case ESP_SLEEP_WAKEUP_UART:
      return "UART";
    default:
      return "OTHER";
  }
}

// Deliberately open/append/close per line and never buffer: a hang later in the
// sleep path must not be able to lose the lines written before it.
void appendLine(const char* line) {
  if (logFull) return;
  HalFile file = Storage.open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("SLPC", "Log open failed: %s", LOG_PATH);
    return;
  }
  if (file.fileSize() > LOG_MAX_SIZE) {
    logFull = true;
    LOG_ERR("SLPC", "Log at cap (%u bytes), no longer appending", static_cast<unsigned>(LOG_MAX_SIZE));
    return;
  }
  file.write(line, strlen(line));
  file.write('\n');
  // No close(): DESTRUCTOR_CLOSES_FILE=1 closes at scope exit.
}

bool loadState() {
  if (!Storage.exists(STATE_PATH)) return false;
  HalFile file = Storage.open(STATE_PATH, O_RDONLY);
  if (!file) return false;
  State loaded{};
  if (file.read(&loaded, sizeof(loaded)) != static_cast<int>(sizeof(loaded))) return false;
  if (loaded.magic != STATE_MAGIC) return false;
  state = loaded;
  return true;
}

void saveState() {
  if (!Storage.exists(STATE_DIR)) {
    Storage.mkdir(STATE_DIR);
  }
  HalFile file = Storage.open(STATE_PATH, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR("SLPC", "State open failed: %s", STATE_PATH);
    return;
  }
  file.write(&state, sizeof(state));
}

// The heart of this module: decide what actually happened to the previous
// sleep. Called only when a sleep record was left open.
void writeVerdict(const esp_reset_reason_t reset, const uint16_t batteryNow) {
  const char* stage = stageName(state.lastStage);
  const bool reachedLatch = state.lastStage >= SleepCrumb::PRE_LATCH;
  // Guard the subtraction: the battery can read higher after a charge.
  const uint16_t drained = (state.batteryAtSleep > batteryNow) ? (state.batteryAtSleep - batteryNow) : 0;

  char line[224];
  snprintf(line, sizeof(line), "  prev sleep #%lu: trigger=%s from=%s reader=%u reached=%s batt %u%% -> %u%% (-%u%%)",
           static_cast<unsigned long>(state.sleepSeq), state.trigger, state.activity, state.fromReader, stage,
           state.batteryAtSleep, batteryNow, drained);
  appendLine(line);

  if (state.lastStage == SleepCrumb::SLEEP_RETURNED) {
    appendLine("  *** BUG: esp_deep_sleep_start() returned ***");
    return;
  }

  if (reset == ESP_RST_DEEPSLEEP) {
    appendLine(reachedLatch ? "  OK: woke from deep sleep (USB-powered sleep, MCU kept alive)"
                            : "  ANOMALY: woke from deep sleep without ever reaching PRE_LATCH");
    return;
  }

  if (reset == ESP_RST_POWERON) {
    if (!reachedLatch) {
      snprintf(line, sizeof(line), "  ANOMALY: lost power at %s, before the latch was armed", stage);
      appendLine(line);
      return;
    }
    // A working battery latch and a user-forced reset both surface as POWERON,
    // so the reset reason alone cannot separate "slept" from "hung, then the
    // user reset it". The battery delta can: a real power-down costs nothing.
    if (drained >= SUSPICIOUS_DRAIN_PCT) {
      snprintf(line, sizeof(line),
               "  *** DID NOT SLEEP: reached PRE_LATCH but lost %u%% battery. Latch did not cut "
               "power; device stayed awake behind the retained sleep screen. ***",
               drained);
      appendLine(line);
      return;
    }
    appendLine("  OK: battery latch cut power, power button restored it");
    return;
  }

  snprintf(line, sizeof(line), "  *** DID NOT SLEEP: hung at %s, recovered by %s reset. Device stayed awake. ***",
           stage, resetReasonName(reset));
  appendLine(line);
}

}  // namespace

namespace SleepCrumb {

void logBoot(const uint16_t batteryPercent) {
  const bool firstEver = !Storage.exists(LOG_PATH);
  if (!loadState()) {
    state = State{};
    state.magic = STATE_MAGIC;
  }
  state.bootCount++;

  if (firstEver) {
    appendLine("CrossPoint sleep forensics log v2");
    appendLine("Lines marked *** are failures. Send this whole file.");
  }

  const esp_reset_reason_t reset = esp_reset_reason();

  char line[224];
  snprintf(line, sizeof(line), "\n[boot %lu] fw=%s reset=%s wake=%s batt=%u%% heap=%lu min=%lu",
           static_cast<unsigned long>(state.bootCount), CROSSPOINT_VERSION, resetReasonName(reset),
           wakeupCauseName(esp_sleep_get_wakeup_cause()), batteryPercent,
           static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()));
  appendLine(line);

  if (state.lastStage != STAGE_NONE) {
    writeVerdict(reset, batteryPercent);
  }

  state.lastStage = STAGE_NONE;
  armed = false;
  saveState();
}

void logWakeVerify(const bool passed, const unsigned long heldMs, const uint16_t requiredMs) {
  char line[96];
  snprintf(line, sizeof(line), "  wake-verify: %s held=%lums required=%ums", passed ? "PASS" : "FAIL", heldMs,
           requiredMs);
  appendLine(line);
}

void beginSleep(const char* trigger, const char* activityName, const bool fromReader, const uint8_t sleepScreenMode,
                const uint16_t batteryPercent) {
  state.sleepSeq++;
  state.batteryAtSleep = batteryPercent;
  state.fromReader = fromReader ? 1 : 0;
  snprintf(state.trigger, sizeof(state.trigger), "%s", trigger ? trigger : "?");
  snprintf(state.activity, sizeof(state.activity), "%s", activityName ? activityName : "?");
  armed = true;

  char line[224];
  snprintf(line, sizeof(line), "  sleep #%lu: trigger=%s from=%s reader=%u screen=%u batt=%u%%",
           static_cast<unsigned long>(state.sleepSeq), state.trigger, state.activity, state.fromReader,
           sleepScreenMode, batteryPercent);
  appendLine(line);
  mark(SLEEP_REQUEST);
}

void mark(const Stage stage) {
  if (!armed) return;
  // Persist the stage before logging it: the state file is what the next boot
  // reads to decide where a hang happened.
  state.lastStage = stage;
  saveState();

  char line[96];
  snprintf(line, sizeof(line), "    t=%lu %02u %s heap=%lu", millis(), static_cast<unsigned>(stage), stageName(stage),
           static_cast<unsigned long>(ESP.getFreeHeap()));
  appendLine(line);
}

}  // namespace SleepCrumb
