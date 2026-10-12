#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cassert>

// A RenderLock owner lends its cancellation predicate for the duration of one render.
// Other tasks' image decoders must not observe or dereference that borrowed context.
class RenderCancellation {
  using Check = bool (*)(const void*);
  inline static std::atomic<TaskHandle_t> owner{nullptr};
  inline static Check check = nullptr;
  inline static const void* context = nullptr;
  inline static bool cancelled = false;

 public:
  class Scope {
   public:
    Scope(Check predicate, const void* ctx) {
      assert(owner.load() == nullptr);
      check = predicate;
      context = ctx;
      cancelled = false;
      owner.store(xTaskGetCurrentTaskHandle(), std::memory_order_release);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() { owner.store(nullptr, std::memory_order_release); }
  };

  static bool requested() {
    if (owner.load(std::memory_order_acquire) != xTaskGetCurrentTaskHandle()) return false;
    cancelled = cancelled || check(context);
    return cancelled;
  }
};
