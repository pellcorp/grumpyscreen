#ifndef __EXTRUDER_PANEL_H__
#define __EXTRUDER_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "selector.h"
#include "button_container.h"
#include "sensor_container.h"
#include "numpad.h"
#include "lvgl/lvgl.h"
#include <string>
#include <vector>

class ExtruderPanel : public NotifyConsumer {
 public:
  ExtruderPanel(KWebSocketClient &ws, std::mutex &l, Numpad &np);
  ~ExtruderPanel();

  void foreground();
  void background();
  void consume(json &j);
  void handle_callback(lv_event_t *e);

  static void _handle_callback(lv_event_t *event) {
    ExtruderPanel *panel = (ExtruderPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  std::vector<std::string> temp_options;
  std::vector<const char*> temp_option_map;
  uint32_t temp_default_idx;
  SensorContainer extruder_temp;
  Selector temp_selector;
  ButtonContainer load_btn;
  ButtonContainer unload_btn;
  // the right-hand tiles share a column of their own, spanning every row, so
  // the header can stay the height of the readout while they still get a full
  // tile's worth of room each. Declared before them: it is their parent.
  lv_obj_t *right_col;
  ButtonContainer cooldown_btn;
  ButtonContainer back_btn;
};

#endif // __EXTRUDER_PANEL_H__
