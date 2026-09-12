#ifndef __SETTING_PANEL_H__
#define __SETTING_PANEL_H__

#include "platform.h"
#include "wifi_panel.h"
#include "sysinfo_panel.h"
#include "button_container.h"
#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <mutex>

class SettingPanel {
 public:
  SettingPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent);
  ~SettingPanel();

  lv_obj_t *get_container();
  void foreground();

  void handle_callback(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    SettingPanel *panel = (SettingPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  static void _tabview_event_cb(lv_event_t *event);
  void refresh_active_tab();

  KWebSocketClient &ws;
  bool owns_cont;
  lv_obj_t *cont;
  lv_obj_t *tabview;
  lv_obj_t *network_tab;
  lv_obj_t *tools_tab;
  lv_obj_t *info_tab;
  lv_obj_t *tools_cont;
  WifiPanel wifi_panel;
  SysInfoPanel sysinfo_panel;

  ButtonContainer restart_klipper_btn;
  ButtonContainer restart_firmware_btn;
  ButtonContainer guppy_restart_btn;
  ButtonContainer support_zip_btn;
  ButtonContainer switch_to_stock_btn;
  ButtonContainer factory_reset_btn;
#ifdef COSMOS
  ButtonContainer update_btn;
#else
  ButtonContainer shutdown_host_btn;
#endif
};

#endif // __SETTING_PANEL_H__
