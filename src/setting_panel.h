#ifndef __SETTING_PANEL_H__
#define __SETTING_PANEL_H__

#include "platform.h"
#include "wifi_panel.h"
#include "button_container.h"
#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <mutex>

class SettingPanel {
 public:
  SettingPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent);
  ~SettingPanel();

  lv_obj_t *get_container();

  void handle_callback(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    SettingPanel *panel = (SettingPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;

  WifiPanel wifi_panel;

  ButtonContainer wifi_btn;
  ButtonContainer restart_klipper_btn;
  ButtonContainer restart_firmware_btn;
  ButtonContainer guppy_restart_btn;
  ButtonContainer support_zip_btn;
  ButtonContainer guppy_update_btn;
  ButtonContainer switch_to_stock_btn;
  ButtonContainer factory_reset_btn;
};

#endif // __SETTING_PANEL_H__
