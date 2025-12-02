#include "sysinfo_panel.h"
#include "utils.h"
#include "config.h"
#include "theme.h"
#include "logger.h"
#include "guppyscreen.h"
#include "subprocess.hpp"
#include "simple_dialog.h"

#include <algorithm>
#include <iterator>
#include <map>

#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;
namespace sp = subprocess;

LV_IMG_DECLARE(back);
LV_IMG_DECLARE(sd_img);

std::vector<std::string> SysInfoPanel::log_levels = {
  "trace",
  "debug",
  "info"
};

std::vector<std::string> SysInfoPanel::themes = {
  "blue",
  "red",
  "green",
  "purple",
  "pink",
  "yellow"
};

static std::map<int32_t, std::string> sleepsec_to_dd_idx = {
  {-1, "Never"}, // never
  {300, "5 Minutes"}, // 5 min
  {600, "10 Minutes"}, // 10 min
  {1800, "30 Minutes"}, // 30 min
  {3600, "1 Hour"}, // 1 hour
  {18000, "5 Hours"} // 5 hour
};

SysInfoPanel::SysInfoPanel()
  : cont(lv_obj_create(lv_scr_act()))
  , left_cont(lv_obj_create(cont))
  , right_cont(lv_obj_create(cont))
  , network_label(lv_label_create(right_cont))

  , disp_sleep_cont(lv_obj_create(left_cont))
  , display_sleep_dd(lv_label_create(disp_sleep_cont))

    // log level
  , ll_cont(lv_obj_create(left_cont))
  , loglevel_dd(lv_label_create(ll_cont))
  , loglevel(1)

    // estop prompt
  , estop_toggle_cont(lv_obj_create(left_cont))
  , prompt_estop_toggle(lv_label_create(estop_toggle_cont))

    // Z axis icons
  , z_icon_toggle_cont(lv_obj_create(left_cont))
  , z_icon_toggle(lv_label_create(z_icon_toggle_cont))

  // theme
  , theme_cont(lv_obj_create(left_cont))
  , theme_dd(lv_label_create(theme_cont))
  , theme(0)

  , support_zip_btn(cont, &sd_img, "Support ZIP", &SysInfoPanel::_handle_callback, this)
  , back_btn(cont, &back, "Back", &SysInfoPanel::_handle_callback, this)
{
  lv_obj_move_background(cont);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);

  lv_obj_clear_flag(left_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(left_cont, LV_PCT(50), LV_PCT(100));
  lv_obj_set_flex_flow(left_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(left_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_clear_flag(right_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(right_cont, LV_PCT(50), LV_PCT(100));
  lv_obj_set_flex_flow(right_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(right_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  Config *conf = Config::get_instance();

  lv_obj_t *l = lv_label_create(disp_sleep_cont);
  lv_obj_set_size(disp_sleep_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(disp_sleep_cont, 0, 0);
  lv_label_set_text(l, "Display Sleep: ");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(display_sleep_dd, LV_ALIGN_RIGHT_MID, 0, 0);

  const int32_t sleep_sec = conf->get<int32_t>("/ui/display_sleep_sec");
  lv_label_set_text(display_sleep_dd, (std::to_string(sleep_sec) + " seconds").c_str());

  lv_obj_set_size(ll_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(ll_cont, 0, 0);
  l = lv_label_create(ll_cont);
  lv_label_set_text(l, "Log Level: ");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(loglevel_dd, LV_ALIGN_RIGHT_MID, 0, 0);

  const std::string log_level = conf->get<std::string>("/ui/log_level");
  auto ll_idx = std::find(log_levels.begin(), log_levels.end(), log_level);
  if (ll_idx != std::end(log_levels)) {
    lv_label_set_text(loglevel_dd, (log_level + "").c_str());
  }

  lv_obj_set_size(estop_toggle_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(estop_toggle_cont, 0, 0);
  l = lv_label_create(estop_toggle_cont);
  lv_label_set_text(l, "Emergency Stop: ");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(prompt_estop_toggle, LV_ALIGN_RIGHT_MID, 0, 0);

  const bool prompt_emergency_stop = conf->get<bool>("/ui/prompt_emergency_stop");
  if (prompt_emergency_stop) {
  	lv_label_set_text(prompt_estop_toggle, "Prompt");
  } else {
    lv_label_set_text(prompt_estop_toggle, "No Prompt");
  }

  lv_obj_set_size(z_icon_toggle_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(z_icon_toggle_cont, 0, 0);
 l = lv_label_create(z_icon_toggle_cont);
  lv_label_set_text(l, "Z Icon: ");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(z_icon_toggle, LV_ALIGN_RIGHT_MID, 0, 0);

  const bool invert_z_icon = conf->get<bool>("/ui/invert_z_icon");
  if (invert_z_icon) {
  	lv_label_set_text(z_icon_toggle, "Inverted");
  } else {
    lv_label_set_text(z_icon_toggle, "Not Inverted");
  }

  lv_obj_set_size(theme_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(theme_cont, 0, 0);
  l = lv_label_create(theme_cont);
  lv_label_set_text(l, "Theme: ");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(theme_dd, LV_ALIGN_RIGHT_MID, 0, 0);

  const std::string theme_id = conf->get<std::string>("/ui/theme");
  auto theme_idx = std::find(themes.begin(), themes.end(), theme_id);
  if (theme_idx != std::end(themes)) {
  	lv_label_set_text(theme_dd, theme_id.c_str());
  }

  lv_obj_add_flag(support_zip_btn.get_container(), LV_OBJ_FLAG_FLOATING);
  lv_obj_align(support_zip_btn.get_container(), LV_ALIGN_BOTTOM_LEFT, 0, 0);
  auto support_zip_cmd = conf->get<std::string>("/commands/support_zip_cmd");
  if (support_zip_cmd == "") {
    support_zip_btn.disable();
  }
  lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

SysInfoPanel::~SysInfoPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void SysInfoPanel::foreground() {
  lv_obj_move_foreground(cont);

  auto ifaces = KUtils::get_interfaces();
  std::vector<std::string> network_detail;
  network_detail.push_back("Network");
  for (auto &iface : ifaces) {
    if (iface != "lo") {
      auto ip = KUtils::interface_ip(iface);
      network_detail.push_back(fmt::format("\t{}: {}", iface, ip));
    }
  }
  lv_label_set_text(network_label, fmt::format("{}\n\nGrumpyScreen\n\tBranch: {}\n\tRevision: {}", join(network_detail, "\n"),
          GUPPYSCREEN_BRANCH, GUPPYSCREEN_VERSION).c_str());
}

void SysInfoPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);

    if (btn == back_btn.get_container()) {
      lv_obj_move_background(cont);
    } else if (btn == support_zip_btn.get_container()) {
      Config *conf = Config::get_instance();
      auto support_zip_cmd = conf->get<std::string>("/commands/support_zip_cmd");
      if (support_zip_cmd != "") {
        auto ret = sp::call(support_zip_cmd);
        if (ret == 0) {
          create_simple_dialog(lv_scr_act(), "Support ZIP Success", "The support.zip can be found in the config directory!", true);
        } else {
          create_simple_dialog(lv_scr_act(), "Support ZIP Failed", "Failed to generate a support zip!", true);
        }
      }
    }
  }
}
