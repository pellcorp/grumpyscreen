#ifndef __UPDATE_MANAGER_CLIENT_H__
#define __UPDATE_MANAGER_CLIENT_H__

#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <mutex>
#include <string>

// Runs the COSMOS update through Moonraker's update_manager the way Fluidd
// and Mainsail do: check for an update, ask before installing it, then show
// the whole of Moonraker's output in a full-screen console that can only be
// closed once the update is over. Updates started from those clients open the
// console here too.
class UpdateManagerClient {
 public:
  UpdateManagerClient(KWebSocketClient &ws, std::mutex &lock);

  // Checks for a COSMOS update and offers it. Call from the UI thread.
  void start();

 private:
  void handle_status(json &j);
  void run_update();
  void handle_notification(json &j);

  void open_console();
  void append(const std::string &text);
  void finish(bool failed);
  void close_console();

  static void confirm_cb(lv_obj_t *mbox, uint32_t button_idx, void *user_data);
  static void ok_cb(lv_event_t *e);

  KWebSocketClient &ws;
  std::mutex &lv_lock;

  lv_obj_t *checking = nullptr;   // "Checking for updates..." while Moonraker refreshes
  lv_obj_t *console = nullptr;    // the full-screen update console
  lv_obj_t *title = nullptr;
  lv_obj_t *log_cont = nullptr;
  lv_obj_t *log_label = nullptr;
  lv_obj_t *ok_btn = nullptr;
  std::string log_text;
  // Set once Moonraker has said the update is over, by its final
  // notify_update_response or its answer to the request.
  bool completed = false;
};

#endif // __UPDATE_MANAGER_CLIENT_H__
