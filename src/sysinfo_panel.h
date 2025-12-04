#ifndef __SYSINFO_PANEL_H__
#define __SYSINFO_PANEL_H__

#include "button_container.h"
#include "lvgl/lvgl.h"

#include <vector>
#include <string>

class SysInfoPanel {
 public:
  SysInfoPanel(lv_obj_t *parent);
  ~SysInfoPanel();

  void foreground();

 private:
  lv_obj_t *cont;
  lv_obj_t *left_cont;
  lv_obj_t *right_cont;
  lv_obj_t *network_label;
  lv_obj_t *settings_label;

  static std::vector<std::string> log_levels;
  static std::vector<std::string> themes;
};

#endif //__SYSINFO_PANEL_H__
