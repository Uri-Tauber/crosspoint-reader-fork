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
  inline static Check imageCheck = nullptr;
  enum class Stop { None, Images, Page };
  inline static Stop stop = Stop::None;

  static Stop poll() {
    if (owner.load(std::memory_order_acquire) != xTaskGetCurrentTaskHandle()) return Stop::None;
    if (stop != Stop::Page && check(context))
      stop = Stop::Page;
    else if (stop == Stop::None && imageCheck && imageCheck(context))
      stop = Stop::Images;
    return stop;
  }

 public:
  class Scope {
   public:
    Scope(Check predicate, const void* ctx, Check imagePredicate = nullptr) {
      assert(owner.load() == nullptr);
      check = predicate;
      context = ctx;
      imageCheck = imagePredicate;
      stop = Stop::None;
      owner.store(xTaskGetCurrentTaskHandle(), std::memory_order_release);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() { owner.store(nullptr, std::memory_order_release); }
  };

  static bool requested() { return poll() == Stop::Page; }
  static bool imagesRequested() { return poll() != Stop::None; }
};
