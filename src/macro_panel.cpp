#include "macro_panel.h"
#include "config.h"
#include "icons.h"
#include "logger.h"
#include "theme.h"

#include <map>

using namespace Theme;

static lv_coord_t grid_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST};

static const std::map<std::string, const lv_img_dsc_t *> macro_icons = {
  {"arrow_down", Icons::ARROW_DOWN}, {"arrow_left", Icons::ARROW_LEFT},
  {"arrow_right", Icons::ARROW_RIGHT}, {"arrow_up", Icons::ARROW_UP},
  {"back", Icons::BACK}, {"bed", Icons::BED}, {"cancel", Icons::CANCEL},
  {"clock", Icons::CLOCK_IMG}, {"cooldown", Icons::COOLDOWN_IMG}, {"delete", Icons::DELETE_IMG},
  {"emergency", Icons::EMERGENCY}, {"extrude", Icons::EXTRUDE_IMG}, {"extruder", Icons::EXTRUDER},
  {"factory_reset", Icons::FACTORY_RESET_IMG}, {"fan", Icons::FAN}, {"fan_off", Icons::FAN_OFF_IMG},
  {"fan_on", Icons::FAN_ON}, {"filament", Icons::FILAMENT_IMG}, {"fine_tune", Icons::FINE_TUNE_IMG},
  {"firmware", Icons::FIRMWARE_IMG}, {"flow", Icons::EXTRUDE}, {"flow_up", Icons::FLOW_UP_IMG},
  {"gui", Icons::GUI_IMG}, {"heater", Icons::HEATER}, {"home", Icons::HOME}, {"home_z", Icons::HOME_Z},
  {"hourglass", Icons::HOURGLASS}, {"infinity", Icons::INFINITY_IMG}, {"info", Icons::INFO_IMG},
  {"layers", Icons::LAYERS_IMG}, {"light", Icons::LIGHT_IMG}, {"light_off", Icons::LIGHT_OFF},
  {"load_filament", Icons::LOAD_FILAMENT_IMG}, {"motor_off", Icons::MOTOR_OFF_IMG},
  {"move", Icons::MOVE}, {"pa_plus", Icons::PA_PLUS_IMG}, {"pause", Icons::PAUSE_IMG},
  {"print", Icons::PRINT}, {"refresh", Icons::REFRESH_IMG}, {"restart", Icons::RESTART_IMG},
  {"resume", Icons::RESUME}, {"retract", Icons::RETRACT_IMG}, {"sd", Icons::SD_IMG},
  {"shutdown", Icons::SHUTDOWN_IMG}, {"speed_up", Icons::SPEED_UP_IMG},
  {"spoolman", Icons::SPOOLMAN_IMG}, {"support_zip", Icons::SUPPORT_ZIP_IMG},
  {"switch_stock", Icons::SWITCH_STOCK_IMG}, {"unload_filament", Icons::UNLOAD_FILAMENT_IMG},
  {"update", Icons::UPDATE_IMG}, {"z_closer", Icons::Z_CLOSER}, {"z_farther", Icons::Z_FARTHER},
};

static const lv_img_dsc_t *macro_icon(const std::string &name) {
  auto it = macro_icons.find(name);
  if (it != macro_icons.end()) return it->second;
  LOG_ERROR("Unknown macro icon '{}', using refresh", name);
  return Icons::REFRESH_IMG;
}

MacroPanel::MacroPanel(lv_obj_t *parent, std::function<void(const std::string &)> send_gcode)
  : send(std::move(send_gcode))
  , grid(create_screen(parent))
{
  for (auto &m : Config::get_instance()->get_objects("/macro")) {
    std::string gcode = m.value("id", std::string());
    if (gcode.empty()) continue;
    std::string name = m.value("display_name", gcode);
    const lv_img_dsc_t *icon = macro_icon(m.value("icon", std::string("refresh")));
    buttons.push_back(std::make_unique<ButtonContainer>(grid, icon, name.c_str(),
                                                        &MacroPanel::handle_click, this));
    gcodes.push_back(gcode);
  }

  if (buttons.empty()) {
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_label_set_text(lv_label_create(grid), "No macros configured");
    return;
  }

  const size_t cols = 4;
  row_dsc.assign((buttons.size() + cols - 1) / cols, LV_GRID_CONTENT);
  row_dsc.push_back(LV_GRID_TEMPLATE_LAST);
  lv_obj_set_grid_dsc_array(grid, grid_col_dsc, row_dsc.data());

  for (size_t i = 0; i < buttons.size(); i++) {
    lv_obj_t *tile = buttons[i]->get_container();
    buttons[i]->use_card();
    lv_obj_set_height(tile, scale_r(92));
    lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, i % cols, 1,
                         LV_GRID_ALIGN_STRETCH, i / cols, 1);
  }
  manage_scroll(grid);
}

void MacroPanel::handle_click(lv_event_t *event) {
  MacroPanel *panel = (MacroPanel *)event->user_data;
  lv_obj_t *tile = lv_event_get_current_target(event);
  for (size_t i = 0; i < panel->buttons.size(); i++) {
    if (panel->buttons[i]->get_container() == tile) {
      panel->send(panel->gcodes[i]);
      return;
    }
  }
}
