#ifndef __EXTRUDER_PANEL_H__
#define __EXTRUDER_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "spoolman_panel.h"
#include "selector.h"
#include "button_container.h"
#include "sensor_container.h"
#include "numpad.h"
#include "lvgl/lvgl.h"
#include <string>
#include <vector>

class ExtruderPanel : public NotifyConsumer {
 public:
  ExtruderPanel(KWebSocketClient &ws, std::mutex &l, Numpad &np, SpoolmanPanel &sm);
  ~ExtruderPanel();

  void foreground();
  void enable_spoolman();  
  void consume(json &j);
  void handle_callback(lv_event_t *e);

  static void _handle_callback(lv_event_t *event) {
    ExtruderPanel *panel = (ExtruderPanel*)event->user_data;
    panel->handle_callback(event);
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
  SensorContainer extruder_temp;
  Selector temp_selector;
  Selector length_selector;
  Selector speed_selector;
  ButtonContainer load_btn;
  ButtonContainer unload_btn;
  ButtonContainer cooldown_btn;
  // the right-hand tiles share a column of their own, spanning every row, so
  // the header can stay the height of the readout while they still get a full
  // tile's worth of room each. Declared before them: it is their parent.
  lv_obj_t *right_col;
  ButtonContainer spoolman_btn;
  ButtonContainer extrude_btn;
  ButtonContainer retract_btn;
  ButtonContainer back_btn;
};

#endif // __EXTRUDER_PANEL_H__
