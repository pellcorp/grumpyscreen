#include "extruder_panel.h"
#include "state.h"
#include "config.h"
#include "logger.h"
#include "icons.h"
#include "theme.h"

using namespace Theme;

#include <algorithm>
#include <cctype>
#include <limits>

namespace {

std::string trim_copy(std::string s) {
  auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_ws(c); }));
  s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_ws(c); }).base(), s.end());
  return s;
}

std::vector<std::string> parse_selector_options(const std::string &csv) {
  std::vector<std::string> options;
  size_t start = 0;
  while (start <= csv.size()) {
    size_t end = csv.find(',', start);
    std::string token = trim_copy(csv.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (!token.empty()) {
      options.push_back(token);
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return options;
}

std::vector<std::string> load_selector_options(const std::string &config_key, size_t max_options) {
  Config *conf = Config::get_instance();
  auto options = parse_selector_options(conf->get<std::string>(config_key));
  if (options.size() > max_options) {
    LOG_INFO("{} has {} values; truncating to {}", config_key, options.size(), max_options);
    options.resize(max_options);
  }
  return options;
}

std::vector<const char*> build_selector_map(std::vector<std::string> &options) {
  std::vector<const char*> map;
  map.reserve(options.size() + 1);
  for (auto &option : options) {
    map.push_back(option.c_str());
  }
  map.push_back("");
  return map;
}

std::string read_config_value_as_string(const std::string &config_key) {
  Config *conf = Config::get_instance();
  std::string configured_value = trim_copy(conf->get<std::string>(config_key));
  if (!configured_value.empty()) {
    return configured_value;
  }

  int configured_int = conf->get<int>(config_key, std::numeric_limits<int>::min());
  if (configured_int != std::numeric_limits<int>::min()) {
    return std::to_string(configured_int);
  }

  return "";
}

uint32_t resolve_default_idx(const std::string &config_key,
                             const std::vector<std::string> &options) {
  std::string configured_value = read_config_value_as_string(config_key);
  if (!configured_value.empty()) {
    auto it = std::find(options.begin(), options.end(), configured_value);
    if (it != options.end()) {
      return static_cast<uint32_t>(std::distance(options.begin(), it));
    }
  }

  if (options.empty()) {
    return std::numeric_limits<uint32_t>::max();
  }

  return 0;
}

} // namespace

ExtruderPanel::ExtruderPanel(KWebSocketClient &websocket_client,
			     std::mutex &lock,
			     Numpad &numpad,
			     SpoolmanPanel &sm)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , panel_cont(create_screen(NULL))
  , spoolman_panel(sm)
  , temp_options(load_selector_options("/ui/extruder_temp_presets", 8))
  , temp_option_map(build_selector_map(temp_options))
  , temp_default_idx(resolve_default_idx("/ui/extruder_temp_default", temp_options))
  , extruder_temp(ws, panel_cont, Icons::EXTRUDER, "Extruder", theme_secondary(), false, true, numpad,
                  "extruder", NULL, NULL)
  , temp_selector(panel_cont, "Extruder Temperature (C)",
		  temp_option_map, temp_default_idx, &ExtruderPanel::_handle_callback, this)
  , load_btn(panel_cont, Icons::LOAD_FILAMENT_IMG, "Load", &ExtruderPanel::_handle_callback, this)
  , unload_btn(panel_cont, Icons::UNLOAD_FILAMENT_IMG, "Unload", &ExtruderPanel::_handle_callback, this)
  , right_col(create_row(panel_cont))
  , spoolman_btn(right_col, Icons::SPOOLMAN_IMG, "Spoolman", &ExtruderPanel::_handle_callback, this)
  , cooldown_btn(right_col, Icons::COOLDOWN_IMG, "Cooldown", &ExtruderPanel::_handle_callback, this)
  , back_btn(right_col, Icons::BACK, "Back", &ExtruderPanel::_handle_callback, this)
{
  lv_obj_move_background(panel_cont);

  // The main section has the readout, a horizontal Load/Unload pair, then the
  // temperature presets. The utility tiles retain their own right-hand column.
  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(6), LV_GRID_FR(6),
    LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(4), LV_GRID_FR(4), LV_GRID_FR(3), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(panel_cont, grid_main_col_dsc, grid_main_row_dsc);

  // Keep the readout at the top of the main section; the action column owns
  // the right edge for the full height of the panel.
  lv_obj_set_grid_cell(extruder_temp.get_sensor(), LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_START, 0, 1);
  ButtonContainer *all_tiles[] = {&spoolman_btn, &back_btn, &load_btn, &unload_btn, &cooldown_btn};
  for (ButtonContainer *b : all_tiles) b->use_card();

  // The three utility actions divide the right-hand column evenly.
  lv_obj_set_grid_cell(right_col, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 3);
  lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
  // Keep the utility tiles as a compact, screen-edge-aligned stack rather
  // than centring their icons in the whole grid column.
  lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_row(right_col, 0, 0);
  for (ButtonContainer *b : {&spoolman_btn, &cooldown_btn, &back_btn}) {
    lv_obj_set_width(b->get_container(), scale_w(90));
    lv_obj_set_height(b->get_container(), 0);
    lv_obj_set_flex_grow(b->get_container(), 1);
  }

  lv_obj_set_grid_cell(load_btn.get_container(), LV_GRID_ALIGN_STRETCH, 0, 1,
                       LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(unload_btn.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1,
                       LV_GRID_ALIGN_STRETCH, 1, 1);
  spoolman_btn.disable();  // state, not style: until moonraker says spoolman is there

  lv_obj_set_grid_cell(temp_selector.get_container(), LV_GRID_ALIGN_STRETCH, 0, 2,
                       LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_set_style_min_height(temp_selector.get_selector(), 0, 0);
  lv_obj_set_style_pad_bottom(temp_selector.get_container(), 0, 0);

  ws.register_notify_update(this);    
}

ExtruderPanel::~ExtruderPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
}

void ExtruderPanel::foreground() {
  lv_obj_move_foreground(panel_cont);
}

void ExtruderPanel::background() {
  lv_obj_move_background(panel_cont);
}

void ExtruderPanel::enable_spoolman() {
  spoolman_btn.enable();
}

void ExtruderPanel::consume(json& j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  auto target_value = j["/params/0/extruder/target"_json_pointer];
  if (!target_value.is_null()) {
    int target = target_value.template get<int>();
    extruder_temp.update_target(target);
  }
  
  auto temp_value = j["/params/0/extruder/temperature"_json_pointer];
  if (!temp_value.is_null()) {   
    int value = temp_value.template get<int>();
    extruder_temp.update_value(value);
  }

  json &pstat_state = j["/params/0/print_stats/state"_json_pointer];
  if (!pstat_state.is_null()) {
    if (pstat_state.template get<std::string>() == "printing") {
      lv_obj_move_background(panel_cont);
    }
  }
}

void ExtruderPanel::handle_callback(lv_event_t *e) {
  LOG_TRACE("handling extruder panel callback");
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *selector = lv_event_get_target(e);
    uint32_t idx = lv_btnmatrix_get_selected_btn(selector);
    const char * v = lv_btnmatrix_get_btn_text(selector, idx);

    if (selector == temp_selector.get_selector()) {
      temp_selector.set_selected_idx(idx);
    }

    LOG_TRACE("selector {} {} {}, temp {}", fmt::ptr(selector), idx, v,
		  fmt::ptr(temp_selector.get_selector()));
    
  } else if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);

    if (btn == back_btn.get_container()) {
      lv_obj_move_background(panel_cont);
    }

    Config *conf = Config::get_instance();
    if (btn == unload_btn.get_container()) {
      if (!unload_btn.start_pressed_transition(2000)) {
        return;
      }
      const std::string unload_filament_macro = conf->get<std::string>("/default_macros/unload_filament");

      const char *temp = lv_btnmatrix_get_btn_text(temp_selector.get_selector(),
                                                   temp_selector.get_selected_idx());
      ws.gcode_script(fmt::format(unload_filament_macro, temp));
    }

    if (btn == load_btn.get_container()) {
      if (!load_btn.start_pressed_transition(2000)) {
        return;
      }
      const std::string load_filament_macro = conf->get<std::string>("/default_macros/load_filament");

      const char *temp = lv_btnmatrix_get_btn_text(temp_selector.get_selector(),
                                                   temp_selector.get_selected_idx());
      ws.gcode_script(fmt::format(load_filament_macro, temp));
    }

    if (btn == cooldown_btn.get_container()) {
      if (!cooldown_btn.start_pressed_transition(1000)) {
        return;
      }
      const std::string cooldown_macro = conf->get<std::string>("/default_macros/cooldown");
      ws.gcode_script(cooldown_macro);
    }

    // disable() clears the clickable flag, so a tap cannot arrive while it is off
    if (btn == spoolman_btn.get_container()) {
      spoolman_panel.foreground();
    }
  }
}
