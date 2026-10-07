#include "update_manager_client.h"
#include "logger.h"
#include "simple_dialog.h"
#include "theme.h"

#include <algorithm>

LV_FONT_DECLARE(dejavusans_mono_14);

// The update_manager entry registered by COSMOS' Moonraker component.
static const char *UPDATE_APP = "cosmos";

// Keep the console to the last few hundred lines; an install logs a lot.
static constexpr size_t LOG_MAX_LINES = 300;

UpdateManagerClient::UpdateManagerClient(KWebSocketClient &c, std::mutex &l)
  : ws(c)
  , lv_lock(l)
{
  ws.register_method_callback("notify_update_response", "UpdateManagerClient",
                              [this](json &j) { this->handle_notification(j); });
}

void UpdateManagerClient::start() {
  if (console != nullptr || checking != nullptr) {
    return;
  }
  checking = create_simple_dialog(lv_scr_act(), UPDATE_BUTTON_TITLE, "Checking for updates...", false, false);

  // A refresh asks GitHub now; Moonraker refuses it while printing or busy,
  // and then the status it already has will do.
  json params = {{"name", UPDATE_APP}};
  ws.send_jsonrpc("machine.update.refresh", params, [this](json &j) {
    if (!j.contains("error")) {
      std::lock_guard<std::mutex> lock(lv_lock);
      handle_status(j);
      return;
    }
    LOG_DEBUG("update_manager refresh refused: {}", j.dump());
    ws.send_jsonrpc("machine.update.status", [this](json &s) {
      std::lock_guard<std::mutex> lock(lv_lock);
      handle_status(s);
    });
  });
}

// Mirrors how Fluidd and Mainsail decide there is an update: commits behind on
// a git style (nightly) channel, a different release on a release channel.
static bool update_available(const json &info) {
  if (info.value("configured_type", std::string()) == "git_repo") {
    return info.value("commits_behind_count", 0) > 0;
  }
  const std::string remote = info.value("remote_version", std::string("?"));
  return remote != "?" && remote != info.value("version", std::string());
}

void UpdateManagerClient::handle_status(json &j) {
  if (checking != nullptr) {
    simple_dialog_close(checking);
    checking = nullptr;
  }

  if (j.contains("error")) {
    const std::string msg = j.value("/error/message"_json_pointer, std::string("Moonraker did not answer"));
    create_simple_dialog(lv_scr_act(), UPDATE_BUTTON_TITLE " Failed", msg.c_str(), true, true);
    return;
  }

  const auto info_ptr = json::json_pointer(std::string("/result/version_info/") + UPDATE_APP);
  if (!j.contains(info_ptr) || !j[info_ptr].is_object()) {
    create_simple_dialog(lv_scr_act(), UPDATE_BUTTON_TITLE " Failed",
                         "Moonraker has no COSMOS updater configured.", true, true);
    return;
  }
  const json &info = j[info_ptr];

  if (j.value("/result/busy"_json_pointer, false)) {
    // an update is already running; its progress shows up in the console
    open_console();
    return;
  }

  const std::string version = info.value("version", std::string("?"));
  if (!info.value("is_valid", true)) {
    std::string msg = "COSMOS cannot be updated from here:";
    for (const auto &w : info.value("warnings", json::array())) {
      if (w.is_string()) {
        msg += "\n" + w.template get<std::string>();
      }
    }
    create_simple_dialog(lv_scr_act(), UPDATE_BUTTON_TITLE " Failed", msg.c_str(), true, true);
    return;
  }

  if (!update_available(info)) {
    const std::string msg = "COSMOS " + version + " is up to date.";
    create_simple_dialog(lv_scr_act(), UPDATE_BUTTON_TITLE, msg.c_str(), true, false);
    return;
  }

  const std::string remote = info.value("remote_version", std::string("?"));
  std::string msg = "Update COSMOS from " + version + " to " + remote + "?";
  const int behind = info.value("commits_behind_count", 0);
  if (behind > 0) {
    msg += "\n\n" + std::to_string(behind) + (behind == 1 ? " new commit" : " new commits") + " on the nightly channel.";
  }
  msg += "\n\nThe printer restarts when the update is installed.";

  static const char *buttons[] = {"Back", "Update", ""};
  SimpleDialogOptions options{};
  options.buttons = buttons;
  options.error = true;
  options.highlighted_button_idx = 1;
  options.result_cb = &UpdateManagerClient::confirm_cb;
  options.user_data = this;
  create_configurable_dialog(lv_scr_act(), UPDATE_BUTTON_TITLE, msg.c_str(), options);
}

void UpdateManagerClient::confirm_cb(lv_obj_t *mbox, uint32_t button_idx, void *user_data) {
  LV_UNUSED(mbox);
  if (button_idx == 1) {
    static_cast<UpdateManagerClient *>(user_data)->run_update();
  }
}

void UpdateManagerClient::run_update() {
  open_console();
  append("Starting update...");

  json params = {{"name", UPDATE_APP}};
  ws.send_jsonrpc("machine.update.client", params, [this](json &j) {
    // Moonraker answers once the request is over. Its progress and final
    // message arrive through notify_update_response before that.
    std::lock_guard<std::mutex> lock(lv_lock);
    if (completed || console == nullptr) {
      return;
    }
    if (j.contains("error")) {
      // refused outright, for example because a print started meanwhile
      append(j.value("/error/message"_json_pointer, std::string("Moonraker refused the update")));
      finish(true);
    } else {
      append("Nothing to install: COSMOS is already up to date.");
      finish(false);
    }
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
  // an update started from another client opens the console as well
  open_console();
  if (!message.empty()) {
    append(message);
  }
  if (complete) {
    // update_manager reports failures as "Error updating <app>: ..."
    finish(message.rfind("Error", 0) == 0);
  }
}

void UpdateManagerClient::open_console() {
  if (console != nullptr) {
    return;
  }
  completed = false;
  log_text.clear();

  // Modal and full-screen: a heading, the log, and an OK button that only
  // works once the update is over.
  console = Theme::create_screen(lv_scr_act());
  lv_obj_add_flag(console, LV_OBJ_FLAG_CLICKABLE);  // eat taps meant for what is underneath
  lv_obj_set_flex_flow(console, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(console, Theme::gap(), 0);
  lv_obj_set_style_pad_row(console, Theme::gap(), 0);

  // the same banner as a dialog's title
  title = lv_label_create(console);
  lv_label_set_text(title, UPDATE_BUTTON_TITLE);
  lv_obj_set_width(title, LV_PCT(100));
  lv_obj_set_style_bg_color(title, Theme::theme_primary(), 0);
  lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(title, Theme::radius_lg(), 0);
  lv_obj_set_style_pad_ver(title, Theme::gap(), 0);
  lv_obj_set_style_pad_hor(title, Theme::popout_pad(), 0);
  lv_obj_set_style_text_font(title, Theme::scale_font(18), 0);
  lv_obj_set_style_text_color(title, Theme::col(Theme::ON_PRIMARY), 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

  // the log: the console tab's look, read only, scrolls with a finger
  log_cont = lv_obj_create(console);
  lv_obj_add_style(log_cont, &Theme::styles().panel, 0);
  lv_obj_set_width(log_cont, LV_PCT(100));
  lv_obj_set_flex_grow(log_cont, 1);
  lv_obj_set_scroll_dir(log_cont, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(log_cont, LV_SCROLLBAR_MODE_AUTO);
  log_label = lv_label_create(log_cont);
  lv_obj_set_width(log_label, LV_PCT(100));
  lv_label_set_long_mode(log_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(log_label, &dejavusans_mono_14, 0);
  lv_label_set_text(log_label, "");

  static const char *ok_map[] = {"OK", ""};
  ok_btn = lv_btnmatrix_create(console);
  lv_btnmatrix_set_map(ok_btn, ok_map);
  lv_obj_set_size(ok_btn, LV_PCT(100), Theme::touch_h());
  lv_btnmatrix_set_btn_ctrl(ok_btn, 0, LV_BTNMATRIX_CTRL_DISABLED);
  lv_obj_add_event_cb(ok_btn, &UpdateManagerClient::ok_cb, LV_EVENT_VALUE_CHANGED, this);
}

void UpdateManagerClient::append(const std::string &text) {
  if (log_label == nullptr) {
    return;
  }
  if (!log_text.empty()) {
    log_text += "\n";
  }
  log_text += text;

  size_t lines = std::count(log_text.begin(), log_text.end(), '\n') + 1;
  size_t cut = 0;
  while (lines > LOG_MAX_LINES) {
    cut = log_text.find('\n', cut) + 1;
    lines--;
  }
  if (cut > 0) {
    log_text.erase(0, cut);
  }

  lv_label_set_text(log_label, log_text.c_str());
  lv_obj_update_layout(log_cont);
  lv_obj_scroll_to_y(log_cont, LV_COORD_MAX, LV_ANIM_OFF);
}

void UpdateManagerClient::finish(bool failed) {
  completed = true;
  if (console == nullptr) {
    return;
  }
  if (failed) {
    lv_label_set_text(title, UPDATE_BUTTON_TITLE " Failed");
    lv_obj_set_style_bg_color(title, Theme::col(Theme::DANGER), 0);
  }
  lv_btnmatrix_clear_btn_ctrl(ok_btn, 0, LV_BTNMATRIX_CTRL_DISABLED);
}

void UpdateManagerClient::ok_cb(lv_event_t *e) {
  UpdateManagerClient *self = static_cast<UpdateManagerClient *>(lv_event_get_user_data(e));
  if (self->completed) {
    self->close_console();
  }
}

void UpdateManagerClient::close_console() {
  if (console == nullptr) {
    return;
  }
  lv_obj_del_async(console);
  console = nullptr;
  title = nullptr;
  log_cont = nullptr;
  log_label = nullptr;
  ok_btn = nullptr;
  log_text.clear();
}
