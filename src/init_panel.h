#ifndef __INIT_PANEL_H__
#define __INIT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "main_panel.h"
#include "print_status_panel.h"

#include <mutex>

class InitPanel {
 public:
  InitPanel(MainPanel &mp, std::mutex &l);
  ~InitPanel();

  void connected(KWebSocketClient &ws);
  void disconnected(KWebSocketClient &ws);
  void set_message(const char *message);

 private:
  void update_visibility();
  static void _tabview_event_cb(lv_event_t *event);
  lv_obj_t *cont;
  lv_obj_t *label;
  MainPanel &main_panel;
  std::mutex &lv_lock;
  bool waiting = true;
};

#endif // __INIT_PANEL_H__
