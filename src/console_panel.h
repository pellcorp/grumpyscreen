#ifndef __CONSOLE_PANEL_H__
#define __CONSOLE_PANEL_H__

#include "button_container.h"
#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <mutex>
#include <list>

class ConsolePanel {
 public:
  ConsolePanel(KWebSocketClient &ws, std::mutex &lock, lv_obj_t *parent);
  ~ConsolePanel();

  lv_obj_t *get_container();
  void foreground();
  void handle_macro_response(json &d);
  void handle_delete_btn(lv_event_t *event);

  static void _handle_delete_btn(lv_event_t *event) {
    ConsolePanel *panel = (ConsolePanel*)event->user_data;
    panel->handle_delete_btn(event);
  };

 private:
  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *console_cont;
  lv_obj_t *top_cont;
  lv_obj_t *output;
  lv_obj_t *bottom_cont;
  lv_obj_t *input;
  ButtonContainer delete_btn;
};

#endif // __CONSOLE_PANEL_H__
