#ifndef __EXTRUDER_PANEL_H__
#define __EXTRUDER_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "spoolman_panel.h"
#include "selector.h"
#include "sensor_container.h"
#include "numpad.h"
#include "button_container.h"
#include "lvgl/lvgl.h"
#include <string>
#include <vector>

class ExtruderPanel : public NotifyConsumer {
 public:
  ExtruderPanel(KWebSocketClient &ws, std::mutex &l, Numpad &np, SpoolmanPanel &sm);
  ~ExtruderPanel();

  void foreground();
  void background();
  void enable_spoolman();
  void show_spoolman(bool shown);
  void consume(json &j);
  void handle_callback(lv_event_t *e);
  void handle_extruder_select(lv_event_t *e);

  static void _handle_callback(lv_event_t *event) {
    ExtruderPanel *panel = (ExtruderPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_extruder_select(lv_event_t *event) {
    ExtruderPanel *panel = (ExtruderPanel*)event->user_data;
    panel->handle_extruder_select(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  SpoolmanPanel &spoolman_panel;
  std::vector<std::string> temp_options;
  std::vector<const char*> temp_option_map;
  uint32_t temp_default_idx;
  std::vector<std::string> length_options;
  std::vector<const char*> length_option_map;
  uint32_t length_default_idx;
  std::vector<std::string> speed_options;
  std::vector<const char*> speed_option_map;
  uint32_t speed_default_idx;

  // the readout is the only sign the load / extrude macros are heating and running
  lv_obj_t *header;
  SensorContainer extruder_temp;
  // picks the active extruder, shown only when there is more than one
  lv_obj_t *extruder_dd;
  std::vector<std::string> extruders;
  std::string active_extruder = "extruder";

  void refresh_extruders();
  void set_active_extruder(const std::string &name);

  // Load / Unload and Extrude / Retract each get a tab; the tiles that apply
  // to both keep a column to the right of the tabview.
  lv_obj_t *tabview;
  lv_obj_t *load_tab;
  lv_obj_t *extrude_tab;
  lv_obj_t *load_cont;
  lv_obj_t *extrude_cont;
  lv_obj_t *extrude_col;  // the Extrude / Retract tiles, stacked

  Selector load_temp_selector;
  ButtonContainer load_btn;
  ButtonContainer unload_btn;

  Selector speed_selector;
  Selector length_selector;
  Selector extrude_temp_selector;
  ButtonContainer extrude_btn;
  ButtonContainer retract_btn;

  lv_obj_t *right_col;
  ButtonContainer spoolman_btn;
  ButtonContainer cooldown_btn;
  ButtonContainer back_btn;
};

#endif // __EXTRUDER_PANEL_H__
