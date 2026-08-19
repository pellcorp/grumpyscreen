#ifndef __WIFI_PANEL_H__
#define __WIFI_PANEL_H__

#include "wpa_event.h"
#include "button_container.h"
#include "lvgl/lvgl.h"
#include <functional>
#include <mutex>

#include <map>
#include <set>
#include <string>

struct WifiPanelOptions {
  lv_obj_t *parent = nullptr;
  bool show_back_button = true;
  const char *footer_text = nullptr;
  std::function<void()> on_back;
};

class WifiPanel {
 public:
  WifiPanel(std::mutex &l, const WifiPanelOptions &options = {});
  
  ~WifiPanel();

  void foreground();
  void handle_back_btn(lv_event_t *event);
  void handle_refresh_btn(lv_event_t *event);
  void handle_callback(lv_event_t *event);
  void remove_network(lv_event_t *event);
  void handle_wpa_event(const std::string &events);
  void handle_kb_input(lv_event_t *e);
  void connect(const char *);
  bool find_current_network();
  void start_ip_poll();
  void stop_ip_poll();
  void update_connection_status_label(const std::string &network_name);
  void handle_ip_poll_timer();
  void restart_wifi();

  static void _handle_back_btn(lv_event_t *event) {
    WifiPanel *panel = (WifiPanel*)event->user_data;
    panel->handle_back_btn(event);
  };

  static void _handle_refresh_btn(lv_event_t *event) {
    WifiPanel *panel = (WifiPanel*)event->user_data;
    panel->handle_refresh_btn(event);
  };

  static void _handle_callback(lv_event_t *event) {
    WifiPanel *panel = (WifiPanel*)event->user_data;
    panel->handle_callback(event);
  };
  
  static void _handle_kb_input(lv_event_t *e) {
    WifiPanel *panel = (WifiPanel*)e->user_data;
    panel->handle_kb_input(e);
  };

  static void _remove_network(lv_event_t *e) {
    WifiPanel *panel = (WifiPanel*)e->user_data;
    panel->remove_network(e);
  };

  static void _handle_ip_poll_timer(lv_timer_t *timer) {
    WifiPanel *panel = static_cast<WifiPanel *>(timer->user_data);
    panel->handle_ip_poll_timer();
  }

 private:
  std::mutex &lv_lock;
  WpaEvent wpa_event;
  lv_timer_t *ip_poll_timer = nullptr;
  lv_obj_t *cont;
  lv_obj_t *spinner;
  lv_obj_t *top_cont;
  lv_obj_t *wifi_table;
  lv_obj_t *wifi_right;
  lv_obj_t *prompt_cont;
  lv_obj_t *wifi_label;
  lv_obj_t *password_input;
  lv_obj_t *footer_label;
  std::function<void()> on_back;
  ButtonContainer back_btn;
  ButtonContainer refresh_btn;
  lv_obj_t *kb;
  std::string selected_network;
  std::string cur_network;
  std::map<std::string, std::string> list_networks;
  std::map<std::string, int> wifi_name_db;
  bool entering_password = false;
  bool waiting_for_ip = false;
  std::string restart_wifi_from_network;

};

#endif // __WIFI_PANEL_H__
