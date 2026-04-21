#include "wake_input_guard.h"

#include <atomic>

#include "lvgl/lvgl.h"

namespace {
std::atomic_bool g_is_sleeping(false);
std::atomic_bool g_suppress_until_release(false);
std::atomic_bool g_wake_requested(false);
}

extern "C" void wake_input_guard_set_sleeping(int sleeping) {
  g_is_sleeping.store(sleeping != 0);
  if (sleeping != 0) {
    g_wake_requested.store(false);
  }
}

extern "C" int wake_input_guard_filter_pointer(int button_state) {
  if (g_suppress_until_release.load()) {
    if (button_state == LV_INDEV_STATE_REL) {
      g_suppress_until_release.store(false);
    }
    return LV_INDEV_STATE_REL;
  }

  if (g_is_sleeping.load() && button_state == LV_INDEV_STATE_PR) {
    g_wake_requested.store(true);
    g_suppress_until_release.store(true);
    return LV_INDEV_STATE_REL;
  }

  return button_state;
}

extern "C" int wake_input_guard_take_wake_request(void) {
  return g_wake_requested.exchange(false) ? 1 : 0;
}
