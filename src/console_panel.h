#ifndef __CONSOLE_PANEL_H__
#define __CONSOLE_PANEL_H__

#include "button_container.h"
#include "macro_panel.h"
#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <memory>
#include <mutex>
#include <list>

class ConsolePanel {
 public:
  ConsolePanel(KWebSocketClient &ws, std::mutex &lock, lv_obj_t *parent);
  ~ConsolePanel();

  lv_obj_t *get_container();
  void foreground();
  void select_first_tab();
  void set_gcode_input_enabled(bool enabled);
  void handle_macro_response(json &d);
  void handle_delete_btn(lv_event_t *event);
  void handle_input(lv_event_t *event);
  void send_gcode(const std::string &gcode);

  static void _handle_delete_btn(lv_event_t *event) {
    ConsolePanel *panel = (ConsolePanel*)event->user_data;
    panel->handle_delete_btn(event);
  };

  static void _handle_input(lv_event_t *event) {
    static_cast<ConsolePanel *>(event->user_data)->handle_input(event);
  };

 private:
  KWebSocketClient &ws;
  std::mutex &lv_lock;
  // Console and Macros tabs
  lv_obj_t *tabview;
  lv_obj_t *console_cont;
  lv_obj_t *macro_tab;
  lv_obj_t *top_cont;
  lv_obj_t *output;
  lv_obj_t *lower_cont;   // the entry row and its keyboard
  lv_obj_t *bottom_cont;  // the entry row: gcode line and the clear tile
  lv_obj_t *input;
  lv_obj_t *kb;
  ButtonContainer delete_btn;
  std::unique_ptr<MacroPanel> macro_panel;
  bool gcode_input_enabled = false;
};

#endif // __CONSOLE_PANEL_H__
