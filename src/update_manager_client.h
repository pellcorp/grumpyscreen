#ifndef __UPDATE_MANAGER_CLIENT_H__
#define __UPDATE_MANAGER_CLIENT_H__

// Only part of a COSMOS build (COSMOS=true in the Makefile).
#ifdef COSMOS

#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <mutex>
#include <string>

// Runs the COSMOS update through Moonraker's update_manager and shows its
// progress, the same way Mainsail and Fluidd do. Updates started from those
// clients show up on the screen too.
class UpdateManagerClient {
 public:
  UpdateManagerClient(KWebSocketClient &ws, std::mutex &lock);

  // Asks Moonraker to update COSMOS and shows the progress dialog. Call from
  // the UI thread.
  void start();

 private:
  void handle_notification(json &j);
  void show(const std::string &message);
  void finish(const std::string &message, bool failed);
  void dismiss();

  static void dismiss_cb(lv_timer_t *t);

  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *mbox = nullptr;
  lv_timer_t *dismiss_timer = nullptr;
};

#endif // COSMOS
#endif // __UPDATE_MANAGER_CLIENT_H__
