// Only part of a COSMOS build (COSMOS=true in the Makefile).
#ifdef COSMOS
#include "update_manager_client.h"
#include "logger.h"
#include "simple_dialog.h"

// The update_manager entry registered by COSMOS' Moonraker component.
static const char *UPDATE_APP = "cosmos";

// How long the final message stays up after a successful update. A COSMOS
// update reboots the printer before this runs out.
static constexpr uint32_t UPDATE_DONE_DISMISS_MS = 8000;

UpdateManagerClient::UpdateManagerClient(KWebSocketClient &c, std::mutex &l)
  : ws(c)
  , lv_lock(l)
{
  ws.register_method_callback("notify_update_response", "UpdateManagerClient",
                              [this](json &j) { this->handle_notification(j); });
}

void UpdateManagerClient::show(const std::string &message) {
  if (dismiss_timer != nullptr) {
    lv_timer_del(dismiss_timer);
    dismiss_timer = nullptr;
  }
  if (mbox == nullptr) {
    // No buttons: the dialog follows the update and closes itself.
    SimpleDialogOptions options{};
    mbox = create_configurable_dialog(lv_scr_act(), UPDATE_BUTTON_TITLE, message.c_str(), options);
  } else {
    lv_label_set_text(lv_msgbox_get_text(mbox), message.c_str());
  }
}

void UpdateManagerClient::dismiss() {
  dismiss_timer = nullptr;
  if (mbox != nullptr) {
    simple_dialog_close(mbox);
    mbox = nullptr;
  }
}

void UpdateManagerClient::dismiss_cb(lv_timer_t *t) {
  UpdateManagerClient *client = static_cast<UpdateManagerClient *>(t->user_data);
  client->dismiss();
  lv_timer_del(t);
}

void UpdateManagerClient::finish(const std::string &message, bool failed) {
  if (failed) {
    dismiss();
    create_simple_dialog(lv_scr_act(), UPDATE_BUTTON_TITLE " Failed", message.c_str(), true, true);
    return;
  }
  // Leave the last message up for a moment, no button: the printer usually
  // reboots at this point, and if it does not the dialog goes away by itself.
  show(message);
  dismiss_timer = lv_timer_create(dismiss_cb, UPDATE_DONE_DISMISS_MS, this);
  lv_timer_set_repeat_count(dismiss_timer, 1);
}

void UpdateManagerClient::start() {
  show("Starting update...");

  json params = {{"name", UPDATE_APP}};
  ws.send_jsonrpc("machine.update.client", params, [this](json &j) {
    // Moonraker answers straight away only when it refuses the update, for
    // example while printing. Progress comes through notify_update_response.
    if (!j.contains("error")) {
      return;
    }
    std::string msg = j.value("/error/message"_json_pointer, std::string("Moonraker refused the update"));
    LOG_ERROR("update_manager refused the update of {}: {}", UPDATE_APP, msg);
    std::lock_guard<std::mutex> lock(lv_lock);
    finish(msg, true);
  });
}

void UpdateManagerClient::handle_notification(json &j) {
  auto &p = j["/params/0"_json_pointer];
  if (!p.is_object()) {
    return;
  }
  if (p.value("application", std::string()) != UPDATE_APP) {
    return;
  }
  std::string message = p.value("message", std::string());
  bool complete = p.value("complete", false);
  LOG_DEBUG("update_manager {}: {}{}", UPDATE_APP, message, complete ? " (complete)" : "");

  std::lock_guard<std::mutex> lock(lv_lock);
  if (complete) {
    // update_manager reports failures as "Error updating <app>: ..."
    finish(message, message.rfind("Error", 0) == 0);
    return;
  }
  if (!message.empty()) {
    show(message);
  }
}
#endif // COSMOS
