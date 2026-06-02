#ifndef __INIT_PANEL_H__
#define __INIT_PANEL_H__

#include "lvgl/lvgl.h"
#include "notify_consumer.h"
#include "websocket_client.h"
#include "main_panel.h"
#include "print_status_panel.h"

#include <mutex>

class InitPanel : public NotifyConsumer {
 public:
  InitPanel(MainPanel &mp, std::mutex &l);
  ~InitPanel();

  void connected(KWebSocketClient &ws);
  void disconnected(KWebSocketClient &ws);
  void set_message(const char *message);
  void consume(json &j) override;

 private:
  lv_obj_t *cont;
  lv_obj_t *label;
  MainPanel &main_panel;
  std::mutex &lv_lock;
};

#endif // __INIT_PANEL_H__
