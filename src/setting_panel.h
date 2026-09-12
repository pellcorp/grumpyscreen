#ifndef __SETTING_PANEL_H__
#define __SETTING_PANEL_H__

#include "platform.h"
#include "button_container.h"
#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <mutex>

class SettingPanel {
 public:
  SettingPanel(KWebSocketClient &c, std::mutex &, lv_obj_t *parent);
  ~SettingPanel();

  lv_obj_t *get_container();
  void foreground();

  void handle_callback(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    SettingPanel *panel = (SettingPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  KWebSocketClient &ws;
  bool owns_cont;
  lv_obj_t *cont;
  lv_obj_t *tabview;
  lv_obj_t *service_tab;
  lv_obj_t *danger_tab;
  lv_obj_t *service_cont;
  lv_obj_t *danger_cont;

  ButtonContainer restart_klipper_btn;
  ButtonContainer restart_firmware_btn;
  ButtonContainer guppy_restart_btn;
  ButtonContainer support_zip_btn;
  ButtonContainer switch_to_stock_btn;
  ButtonContainer factory_reset_btn;
#ifdef COSMOS
  ButtonContainer update_btn;
#endif
  ButtonContainer shutdown_host_btn;
};

#endif // __SETTING_PANEL_H__
