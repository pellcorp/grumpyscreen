#ifndef __FINETINE_PANEL_H__
#define __FINETINE_PANEL_H__

#include "lvgl/lvgl.h"
#include "button_container.h"
#include "selector.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

// Live tuning during a print: one row per parameter (Z offset, pressure
// advance, speed, flow), each a readout beside its down / up / reset buttons,
// with the two step selectors and Back along the bottom.
class FineTunePanel : public NotifyConsumer {
 public:
  FineTunePanel(KWebSocketClient &, std::mutex &);
  ~FineTunePanel();
  void foreground();
  void consume(json &j);

 private:
  // a parameter row: a readout card (icon, name, value) and its -, + and
  // Reset buttons, one grid cell each
  struct Row {
    lv_obj_t *card = NULL;
    lv_obj_t *value = NULL;
    lv_obj_t *down = NULL, *up = NULL, *reset = NULL;
  };
  void make_row(Row &r, int grid_row, const void *icon, const char *name, const char *initial);
  void update_values(const json &state);
  void handle_btn(lv_obj_t *btn);
  void handle_selector(lv_event_t *e);

  static void _handle_btn(lv_event_t *e) {
    static_cast<FineTunePanel *>(lv_event_get_user_data(e))->handle_btn(lv_event_get_current_target(e));
  }
  static void _handle_selector(lv_event_t *e) {
    static_cast<FineTunePanel *>(lv_event_get_user_data(e))->handle_selector(e);
  }
  static void _handle_back(lv_event_t *e) {
    lv_obj_move_background(static_cast<FineTunePanel *>(lv_event_get_user_data(e))->panel_cont);
  }

  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  Row z, pa, speed, flow;
  Selector step_selector;        // Z offset and pressure advance step
  Selector multiplier_selector;  // speed and flow step
  ButtonContainer back_btn;
};

#endif  // __FINETINE_PANEL_H__
