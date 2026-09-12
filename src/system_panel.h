#ifndef __SYSTEM_PANEL_H__
#define __SYSTEM_PANEL_H__

#include "wifi_panel.h"
#include "sysinfo_panel.h"
#include "lvgl/lvgl.h"

#include <mutex>

class SystemPanel {
 public:
  SystemPanel(std::mutex &l, lv_obj_t *parent);
  ~SystemPanel();

  void foreground();

 private:
  static void _tabview_event_cb(lv_event_t *event);
  void refresh_active_tab();

  bool owns_cont;
  lv_obj_t *cont;
  lv_obj_t *tabview;
  lv_obj_t *network_tab;
  lv_obj_t *info_tab;
  WifiPanel wifi_panel;
  SysInfoPanel sysinfo_panel;
};

#endif // __SYSTEM_PANEL_H__
