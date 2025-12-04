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

  lv_obj_t *disp_sleep_cont;
  lv_obj_t *display_sleep_dd;

  lv_obj_t *ll_cont;
  lv_obj_t *loglevel_dd;
  uint32_t loglevel;

  lv_obj_t *estop_toggle_cont;
  lv_obj_t *prompt_estop_toggle;

  lv_obj_t *z_icon_toggle_cont;
  lv_obj_t *z_icon_toggle;

  lv_obj_t *theme_cont;
  lv_obj_t *theme_dd;
  uint32_t theme;

  static std::vector<std::string> log_levels;
  static std::vector<std::string> themes;
};

#endif //__SYSINFO_PANEL_H__
