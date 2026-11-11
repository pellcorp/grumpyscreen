#pragma once

#include "platform.h"

#ifdef OS_ANDROID
#include "lvgl/lvgl.h"

namespace BackNavigation {

// Call these functions with the LVGL lock held, or before the UI loop starts.
void register_button(lv_obj_t *obj);

// Clicks the frontmost reachable panel Back button. False opens the printer list.
bool handle();

}  // namespace BackNavigation
#endif
