#ifndef __WIFI_PANEL_H__
#define __WIFI_PANEL_H__

#include "wpa_event.h"
#ifdef GUPPY_BOOTSTRAP
#include "button_container.h"
#endif
#include "lvgl/lvgl.h"
#include <mutex>

#include <map>
#include <set>
#include <string>

struct WifiPanelOptions {
  lv_obj_t *parent = nullptr;
#ifdef GUPPY_BOOTSTRAP
  bool show_refresh_button = true;
  const char *footer_text = nullptr;
#endif
  bool flush = false;
};

class WifiPanel {
 public:
  WifiPanel(std::mutex &l, const WifiPanelOptions &options = {});
  
  ~WifiPanel();

  void foreground();
#ifdef GUPPY_BOOTSTRAP
  void handle_refresh_btn(lv_event_t *event);
#endif
  void handle_callback(lv_event_t *event);
  void remove_network(uint32_t btn_idx);
  void handle_wpa_event(const std::string &events);
  void handle_kb_input(lv_event_t *e);
  void handle_password_visibility(lv_event_t *e);
  void cancel_password_dialog();
  bool connect(const char *);
  bool find_current_network();
  void start_ip_poll();
  void stop_ip_poll();
  void update_connection_status_label(const std::string &network_name);
  void update_ethernet_status();
  void show_connecting_status(const std::string &network_name);
  void set_network_status(const std::string &network_name, const std::string &status,
                          const char *icon);
  void stop_connection_spinner();
  void handle_spinner_timer();
  void draw_table_cell(lv_event_t *event);
  void handle_ip_poll_timer();
  void handle_connection_timeout();
  void restart_wifi();
  void update_password_submit_state();
  void start_network();
  void show_password_dialog();
  void hide_password_dialog();
  void submit_password();
  void show_connection_error(const char *message);
  void discard_pending_network();

#ifdef GUPPY_BOOTSTRAP
  static void _handle_refresh_btn(lv_event_t *event) {
    WifiPanel *panel = (WifiPanel*)event->user_data;
    panel->handle_refresh_btn(event);
  };
#endif

  static void _handle_callback(lv_event_t *event) {
    WifiPanel *panel = (WifiPanel*)event->user_data;
    panel->handle_callback(event);
  };
  
  static void _handle_kb_input(lv_event_t *e) {
    WifiPanel *panel = (WifiPanel*)e->user_data;
    panel->handle_kb_input(e);
  };

  static void _handle_password_visibility(lv_event_t *e) {
    WifiPanel *panel = (WifiPanel*)e->user_data;
    panel->handle_password_visibility(e);
  };

  static void _remove_network(lv_obj_t *, uint32_t btn_idx, void *user_data) {
    static_cast<WifiPanel *>(user_data)->remove_network(btn_idx);
  };

  static void _handle_ip_poll_timer(lv_timer_t *timer) {
    WifiPanel *panel = static_cast<WifiPanel *>(timer->user_data);
    panel->handle_ip_poll_timer();
  }

  static void _handle_connection_timeout(lv_timer_t *timer) {
    WifiPanel *panel = static_cast<WifiPanel *>(timer->user_data);
    panel->handle_connection_timeout();
  }

  static void _handle_spinner_timer(lv_timer_t *timer) {
    static_cast<WifiPanel *>(timer->user_data)->handle_spinner_timer();
  }

  static void _handle_network_start_timer(lv_timer_t *timer) {
    WifiPanel *panel = static_cast<WifiPanel *>(timer->user_data);
    panel->network_start_timer = nullptr;
    panel->start_network();
  }

  static void _draw_table_cell(lv_event_t *event) {
    static_cast<WifiPanel *>(event->user_data)->draw_table_cell(event);
  }

 private:
  std::mutex &lv_lock;
  WpaEvent wpa_event;
  lv_timer_t *ip_poll_timer = nullptr;
  lv_timer_t *connection_timeout_timer = nullptr;
  lv_timer_t *connection_spinner_timer = nullptr;
  lv_timer_t *network_start_timer = nullptr;
  bool owns_cont;
  lv_obj_t *cont;
  lv_obj_t *spinner;
  lv_obj_t *wifi_table;
  lv_obj_t *credential_overlay;
  lv_obj_t *credential_box;
  lv_obj_t *credential_title;
  lv_obj_t *credential_status;
  lv_obj_t *password_row;
  lv_obj_t *password_input;
  lv_obj_t *password_visibility_btn;
#ifdef GUPPY_BOOTSTRAP
  lv_obj_t *footer_label;
  ButtonContainer refresh_btn;
#endif
  lv_obj_t *kb;
  std::string selected_network;
  std::string cur_network;
  std::map<std::string, std::string> list_networks;
  std::map<std::string, int> wifi_name_db;
  bool entering_password = false;
  bool credential_connecting = false;
  bool connection_in_progress = false;
  bool restart_wifi_after_connect = false;
  bool suppress_next_table_selection = false;
  bool waiting_for_ip = false;
  bool has_ethernet = false;
  bool network_started = false;
  uint16_t connection_spinner_row = LV_TABLE_CELL_NONE;
  uint16_t connection_spinner_angle = 0;
  lv_area_t connection_spinner_area{};
  bool connection_spinner_area_valid = false;
  std::string pending_network_id;
  std::string restart_wifi_from_network;

};

#endif // __WIFI_PANEL_H__
