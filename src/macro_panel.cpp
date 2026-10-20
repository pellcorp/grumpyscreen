#include "macro_panel.h"
#include "config.h"
#include "icons.h"
#include "logger.h"
#include "numpad.h"
#include "theme.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <map>
#include <sstream>
#include <string_view>

using namespace Theme;

static lv_coord_t grid_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST};

// FIXME - do we actually need all these icons for macros maybe we should only be supporting a subset
static const std::map<std::string, const lv_img_dsc_t *> macro_icons = {
  {"arrow_down", Icons::ARROW_DOWN}, {"arrow_left", Icons::ARROW_LEFT},
  {"arrow_right", Icons::ARROW_RIGHT}, {"arrow_up", Icons::ARROW_UP},
  {"back", Icons::BACK}, {"bed", Icons::BED}, {"cancel", Icons::CANCEL},
  {"clock", Icons::CLOCK_IMG}, {"cooldown", Icons::COOLDOWN_IMG}, {"delete", Icons::DELETE_IMG},
  {"emergency", Icons::EMERGENCY}, {"extrude", Icons::EXTRUDE_IMG}, {"extruder", Icons::EXTRUDER},
  {"factory_reset", Icons::FACTORY_RESET_IMG}, {"fan", Icons::FAN}, {"fan_off", Icons::FAN_OFF_IMG},
  {"fan_on", Icons::FAN_ON}, {"fine_tune", Icons::FINE_TUNE_IMG},
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

namespace {
std::string trim_copy(std::string value) {
  const auto first = value.find_first_not_of(" \t");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t");
  return value.substr(first, last - first + 1);
}

bool parse_int(std::string_view value, int &out) {
  const char *begin = value.data();
  const char *end = begin + value.size();
  auto result = std::from_chars(begin, end, out);
  return result.ec == std::errc{} && result.ptr == end;
}

// Split on whitespace, except whitespace in a {parameter declaration}.
std::vector<std::string> split_gcode(const std::string &gcode) {
  std::vector<std::string> tokens;
  std::string token;
  int braces = 0;
  for (char c : gcode) {
    if (c == '{') ++braces;
    if (std::isspace(static_cast<unsigned char>(c)) && braces == 0) {
      if (!token.empty()) { tokens.push_back(token); token.clear(); }
    } else {
      token += c;
    }
    if (c == '}' && braces > 0) --braces;
  }
  if (!token.empty()) tokens.push_back(token);
  return tokens;
}
}  // namespace

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
    Macro macro;
    if (parse_macro(gcode, macro)) {
      const bool unsupported = macro.unsupported_parameter_count;
      macros.push_back(std::move(macro));
      if (unsupported) {
        LOG_ERROR("Macro '{}' has more than the supported {} parameters", gcode, max_parameters);
        buttons.back()->disable();
      }
    } else {
      macros.emplace_back();
    }
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
    buttons[i]->use_plain();
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
      if (!panel->macros[i].parameters.empty()) {
        panel->open_popup(i, lv_label_get_text(lv_obj_get_child(tile, 1)));
        return;
      }
      panel->send(panel->gcodes[i]);
      return;
    }
  }
}

bool MacroPanel::parse_macro(const std::string &gcode, Macro &macro) {
  const auto tokens = split_gcode(gcode);
  if (tokens.empty()) return false;

  for (const std::string &token : tokens) {
    const size_t equals = token.find('=');
    if (equals == std::string::npos || equals == 0 || equals + 3 > token.size()
        || token[equals + 1] != '{' || token.back() != '}') {
      macro.literals.push_back(token);
      macro.parts.push_back(static_cast<int>(macro.literals.size() - 1));
      continue;
    }

    const std::string contents = trim_copy(token.substr(equals + 2, token.size() - equals - 3));
    Parameter parameter;
    parameter.name = token.substr(0, equals);
    const size_t comma = contents.find(',');
    if (comma != std::string::npos) {
      parameter.type = Parameter::Type::Choices;
      std::stringstream values(contents);
      std::string value;
      while (std::getline(values, value, ',')) {
        value = trim_copy(value);
        if (value.empty()) return false;
        parameter.choices.push_back(std::move(value));
      }
      if (parameter.choices.empty()) return false;
    } else {
      // A range is exactly {minimum-maximum}; values are inclusive integers.
      const size_t dash = contents.find('-', !contents.empty() && contents.front() == '-' ? 1 : 0);
      if (dash == std::string::npos) return false;
      const std::string low = trim_copy(contents.substr(0, dash));
      const std::string high = trim_copy(contents.substr(dash + 1));
      parameter.type = Parameter::Type::IntegerRange;
      if (!parse_int(low, parameter.minimum) || !parse_int(high, parameter.maximum)
          || parameter.minimum > parameter.maximum) return false;
    }

    if (macro.parameters.size() == max_parameters) {
      macro.unsupported_parameter_count = true;
      return true;
    }
    macro.parameters.push_back(std::move(parameter));
    macro.parts.push_back(-static_cast<int>(macro.parameters.size()));
  }
  return !macro.parameters.empty();
}

void MacroPanel::open_popup(size_t macro_index, const std::string &title) {
  active_macro = macro_index;
  Macro &macro = macros[active_macro];

  popup = lv_obj_create(lv_layer_top());
  lv_obj_add_style(popup, &styles().popout, 0);
  lv_obj_set_size(popup, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *box = lv_obj_create(popup);
  lv_obj_add_style(box, &styles().popout_box, 0);
  lv_obj_set_size(box, popout_w(), popout_max_h());
  lv_obj_center(box);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *heading = lv_label_create(box);
  lv_label_set_text(heading, title.c_str());
  lv_obj_set_style_text_font(heading, scale_font(22), 0);

  lv_obj_t *form = lv_obj_create(box);
  lv_obj_set_width(form, LV_PCT(100));
  lv_obj_set_flex_grow(form, 1);
  lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(form, 0, 0);
  lv_obj_set_style_border_width(form, 0, 0);
  lv_obj_set_style_pad_row(form, gap(), 0);
  lv_obj_clear_flag(form, LV_OBJ_FLAG_SCROLLABLE);

  for (Parameter &parameter : macro.parameters) {
    lv_obj_t *row = create_row(form);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, parameter.name.c_str());
    lv_obj_set_width(label, LV_PCT(42));
    // Numpad toggles LV_STATE_CHECKED on the active label, making it clear
    // which range field its value will be applied to.
    lv_obj_set_style_bg_color(label, col(SELECTED), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(label, theme_primary(), LV_STATE_CHECKED);
    lv_obj_set_style_radius(label, radius_sm(), LV_STATE_CHECKED);
    lv_obj_set_style_pad_hor(label, gap() / 2, LV_STATE_CHECKED);
    parameter.label = label;
    if (parameter.type == Parameter::Type::Choices) {
      parameter.control = lv_dropdown_create(row);
      std::string options;
      for (const std::string &choice : parameter.choices) {
        if (!options.empty()) options += '\n';
        options += choice;
      }
      lv_dropdown_set_options(parameter.control, options.c_str());
      lv_obj_set_flex_grow(parameter.control, 1);
    } else {
      parameter.control = lv_textarea_create(row);
      lv_textarea_set_one_line(parameter.control, true);
      lv_textarea_set_text(parameter.control, std::to_string(parameter.minimum).c_str());
      lv_textarea_set_placeholder_text(parameter.control,
          (std::to_string(parameter.minimum) + " - " + std::to_string(parameter.maximum)).c_str());
      lv_obj_set_flex_grow(parameter.control, 1);
      lv_obj_add_event_cb(parameter.control, &MacroPanel::handle_range_input, LV_EVENT_CLICKED, this);
    }
  }

  lv_obj_t *actions = create_row(box);
  lv_obj_set_width(actions, LV_PCT(100));
  lv_obj_set_height(actions, touch_h());
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *cancel = create_text_btn(actions, "Cancel", &MacroPanel::handle_popup_action, this);
  lv_obj_set_flex_grow(cancel, 1);
  lv_obj_set_height(cancel, LV_PCT(100));
  lv_obj_t *run = create_text_btn(actions, "Run", &MacroPanel::handle_popup_action, this);
  lv_obj_set_flex_grow(run, 1);
  lv_obj_set_height(run, LV_PCT(100));
  lv_obj_set_user_data(cancel, reinterpret_cast<void *>(0));
  lv_obj_set_user_data(run, reinterpret_cast<void *>(1));

  // Keep the actions on the bottom edge.  Giving the form the calculated
  // middle height is more reliable than relying on flex-grow here: the form
  // contains rows which themselves have flex-grow, and LVGL otherwise lets
  // their content height win before distributing the spare space.
  lv_obj_update_layout(box);
  const lv_coord_t form_height = std::max<lv_coord_t>(0,
      lv_obj_get_content_height(box) - lv_obj_get_height(heading) - lv_obj_get_height(actions)
      - 2 * gap());
  lv_obj_set_flex_grow(form, 0);
  lv_obj_set_height(form, form_height);
  const lv_coord_t row_height = macro.parameters.empty() ? 0 :
      (form_height - gap() * static_cast<lv_coord_t>(macro.parameters.size() - 1))
      / static_cast<lv_coord_t>(macro.parameters.size());
  // Dropdowns have a compact natural height while textareas previously used
  // touch_h(), which made the two control types look mismatched.  Use one
  // middle ground, reducing it only if a five-field form needs the space.
  const lv_coord_t field_height = std::max<lv_coord_t>(0, std::min(scale_r(34), row_height));
  const lv_font_t *field_font = scale_font(16);
  for (Parameter &parameter : macro.parameters) {
    lv_obj_set_height(parameter.control, field_height);
    lv_obj_set_style_text_font(parameter.control, field_font, 0);
    if (parameter.type == Parameter::Type::IntegerRange) {
      const lv_coord_t padding = std::max<lv_coord_t>(0,
          (field_height - lv_font_get_line_height(field_font)) / 2
          - lv_obj_get_style_border_width(parameter.control, LV_PART_MAIN));
      lv_obj_set_style_pad_ver(parameter.control, padding, 0);
    }
  }
  lv_obj_update_layout(box);

  // Reuse the same keypad used by heater targets rather than LVGL's generic
  // numeric keyboard (which has cursor keys and a less touch-friendly shape).
  popup_numpad = std::make_unique<Numpad>(popup);
  popup_numpad->cover_square_right();
}

void MacroPanel::handle_range_input(lv_event_t *event) {
  MacroPanel *panel = static_cast<MacroPanel *>(lv_event_get_user_data(event));
  panel->active_range_control = lv_event_get_target(event);
  Macro &macro = panel->macros[panel->active_macro];
  for (const Parameter &parameter : macro.parameters) {
    if (parameter.control != panel->active_range_control) continue;
    panel->popup_numpad->set_callback([panel, &parameter](double value) {
      const int selected = std::clamp(static_cast<int>(value), parameter.minimum, parameter.maximum);
      lv_textarea_set_text(panel->active_range_control, std::to_string(selected).c_str());
    }, parameter.minimum, parameter.maximum);
    panel->popup_numpad->set_highlight_target(parameter.label);
    panel->popup_numpad->foreground_reset();
    return;
  }
}

void MacroPanel::handle_popup_action(lv_event_t *event) {
  MacroPanel *panel = static_cast<MacroPanel *>(lv_event_get_user_data(event));
  if (reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target(event))) != 0) {
    panel->send_popup_macro();
  }
  panel->close_popup();
}

void MacroPanel::send_popup_macro() {
  Macro &macro = macros[active_macro];
  std::vector<std::string> values;
  values.reserve(macro.parameters.size());
  for (const Parameter &parameter : macro.parameters) {
    if (parameter.type == Parameter::Type::Choices) {
      values.push_back(parameter.name + "=" + parameter.choices[lv_dropdown_get_selected(parameter.control)]);
    } else {
      std::string value = trim_copy(lv_textarea_get_text(parameter.control));
      int number = 0;
      if (!parse_int(value, number) || number < parameter.minimum || number > parameter.maximum) {
        number = parameter.minimum;
      }
      values.push_back(parameter.name + "=" + std::to_string(number));
    }
  }

  std::string gcode;
  for (int part : macro.parts) {
    if (!gcode.empty()) gcode += ' ';
    if (part >= 0) gcode += macro.literals[part];
    else gcode += values[-part - 1];
  }
  send(gcode);
}

void MacroPanel::close_popup() {
  // Numpad owns an overlay child of popup, so it must release that child
  // before deleting the popup that contains it.
  popup_numpad.reset();
  if (popup != nullptr) lv_obj_del(popup);
  popup = nullptr;
  active_range_control = nullptr;
}
