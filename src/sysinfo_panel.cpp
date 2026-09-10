#include "sysinfo_panel.h"
#include "utils.h"
#include "config.h"
#include "theme.h"

using namespace Theme;

std::vector<std::string> SysInfoPanel::log_levels = {
  "trace",
  "debug",
  "info"
};

// one line of a panel: a dim section title, or a plain value line. LVGL has
// no tab stops and blank lines are not spacing, so the panels are flex
// columns of these one gap apart.
static lv_obj_t *add_line(lv_obj_t *panel, const std::string &text, bool title = false) {
  lv_obj_t *l = lv_label_create(panel);
  lv_label_set_text(l, text.c_str());
  lv_obj_set_width(l, LV_PCT(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  if (title) lv_obj_add_style(l, &styles().dim_label, 0);
  return l;
}

SysInfoPanel::SysInfoPanel(lv_obj_t *parent)
  : cont(create_screen(parent))  // fills the tab: it is the page
  , left_cont(lv_obj_create(cont))
  , right_cont(lv_obj_create(cont))
{
  lv_obj_move_background(cont);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);

  // two panels side by side, each half the tab
  for (lv_obj_t *side : {left_cont, right_cont}) {
    lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(side, &styles().panel, 0);
    lv_obj_set_height(side, LV_PCT(100));
    lv_obj_set_flex_grow(side, 1);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(side, gap() * 2, 0);  // a readout, not a form: the lines want air
  }

  Config *conf = Config::get_instance();
  add_line(left_cont, "Settings", true);

  const int32_t sleep_sec = conf->get<int32_t>("/ui/display_sleep_sec");
  add_line(left_cont, "Display Sleep: " +
           (sleep_sec == -1 ? std::string("Never") : std::to_string(sleep_sec) + " seconds"));

  const std::string log_level = conf->get<std::string>("/ui/log_level");
  const bool known = std::find(log_levels.begin(), log_levels.end(), log_level) != log_levels.end();
  add_line(left_cont, "Log Level: " + (known ? log_level : std::string("info")));

  add_line(left_cont, std::string("Emergency Stop: ") +
           (conf->get<bool>("/ui/prompt_emergency_stop") ? "Prompt" : "No Prompt"));
}

SysInfoPanel::~SysInfoPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void SysInfoPanel::foreground() {
  lv_obj_move_foreground(cont);

  // rebuilt on every visit so the addresses are current
  lv_obj_clean(right_cont);
  add_line(right_cont, "Network", true);
  for (auto &iface : KUtils::get_interfaces()) {
    if (iface != "lo") {
      add_line(right_cont, iface + ": " + KUtils::interface_ip(iface));
    }
  }
  // a second section: one extra gap above its title sets it apart
  lv_obj_set_style_pad_top(add_line(right_cont, "Version Info", true), gap(), 0);
  add_line(right_cont, std::string("Branch: ") + GUPPYSCREEN_BRANCH);
  add_line(right_cont, std::string("Revision: ") + GUPPYSCREEN_VERSION);
}
