#ifndef __SYSTEM_PANEL_H__
#define __SYSTEM_PANEL_H__

#include "wifi_panel.h"
#include "lvgl/lvgl.h"

#include <mutex>

class SystemPanel {
 public:
  SystemPanel(std::mutex &l, lv_obj_t *parent);

  void foreground();

 private:
  WifiPanel wifi_panel;
};

#endif // __SYSTEM_PANEL_H__
