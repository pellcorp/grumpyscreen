#include "sysinfo_panel.h"
#include "utils.h"
#include "config.h"
#include "theme.h"

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

SysInfoPanel::SysInfoPanel(lv_obj_t *parent)
  : cont(lv_obj_create(parent))
  , left_cont(lv_obj_create(cont))
  , right_cont(lv_obj_create(cont))
  , network_label(lv_label_create(right_cont))
  , settings_label(lv_label_create(left_cont))
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

  std::string settings;
  settings.append("Settings: \n\n");

  settings.append("\tDisplay Sleep: ");
  const int32_t sleep_sec = conf->get<int32_t>("/ui/display_sleep_sec");
  if (sleep_sec == -1) {
    settings.append("Never\n\n");
  } else {
    settings.append(std::to_string(sleep_sec) + " seconds\n\n");
  }

  settings.append("\tLog Level: ");
  const std::string log_level = conf->get<std::string>("/ui/log_level");
  auto ll_idx = std::find(log_levels.begin(), log_levels.end(), log_level);
  if (ll_idx != std::end(log_levels)) {
    settings.append(log_level + "\n\n");
  } else {
    settings.append("info\n\n");
  }

  settings.append("\tEmergency Stop: ");
  const bool prompt_emergency_stop = conf->get<bool>("/ui/prompt_emergency_stop");
  if (prompt_emergency_stop) {
  	settings.append("Prompt\n\n");
  } else {
    settings.append("No Prompt\n\n");
  }

  settings.append("\tZ Icon: ");
  const bool invert_z_icon = conf->get<bool>("/ui/invert_z_icon");
  if (invert_z_icon) {
  	settings.append("Inverted\n\n");
  } else {
    settings.append("Not Inverted\n\n");
  }

  settings.append("\tTheme: ");
  const std::string theme_id = conf->get<std::string>("/ui/theme");
  auto theme_idx = std::find(themes.begin(), themes.end(), theme_id);
  if (theme_idx != std::end(themes)) {
  	settings.append(theme_id + "\n\n");
  } else {
    settings.append("blue\n\n");
  }

  lv_label_set_text(settings_label, settings.c_str());
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
  std::string network_detail;
  network_detail.append("Network:\n\n");
  for (auto &iface : ifaces) {
    if (iface != "lo") {
      auto ip = KUtils::interface_ip(iface);
      network_detail.append(std::string("\t") + iface + ": " + ip + "\n\n");
    }
  }
  network_detail.append("\n");
  network_detail.append("Version Info:\n\n");
  network_detail.append(std::string("\tBranch: ") + GUPPYSCREEN_BRANCH + "\n\n");
  network_detail.append(std::string("\tRevision: ") + GUPPYSCREEN_VERSION + "\n\n");

  lv_label_set_text(network_label, network_detail.c_str());
}
