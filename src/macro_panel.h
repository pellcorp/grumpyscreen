#ifndef __MACRO_PANEL_H__
#define __MACRO_PANEL_H__

#include "button_container.h"
#include "lvgl/lvgl.h"
#include "numpad.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// One button per [macro "GCODE"] section in grumpyscreen.cfg, sent through the console
class MacroPanel {
 public:
  MacroPanel(lv_obj_t *parent, std::function<void(const std::string &)> send);

 private:
  struct Parameter {
    enum class Type { Choices, IntegerRange };

    std::string name;
    Type type;
    std::vector<std::string> choices;
    int minimum = 0;
    int maximum = 0;
    lv_obj_t *label = nullptr;
    lv_obj_t *control = nullptr;
  };

  struct Macro {
    // Each part is either literal gcode or a parameter index encoded as a
    // negative value (-1 is parameter 0).  Keeping the literals means fixed
    // arguments in a macro header survive the round trip unchanged.
    std::vector<std::string> literals;
    std::vector<int> parts;
    std::vector<Parameter> parameters;
    bool unsupported_parameter_count = false;
  };

  static void handle_click(lv_event_t *event);
  static void handle_popup_action(lv_event_t *event);
  static void handle_range_input(lv_event_t *event);
  static bool parse_macro(const std::string &gcode, Macro &macro);
  void open_popup(size_t macro_index, const std::string &title);
  void close_popup();
  void send_popup_macro();

  std::function<void(const std::string &)> send;
  lv_obj_t *grid;
  std::vector<std::unique_ptr<ButtonContainer>> buttons;
  std::vector<std::string> gcodes;
  std::vector<Macro> macros;
  static constexpr size_t max_parameters = 5;
  lv_obj_t *popup = nullptr;
  std::unique_ptr<Numpad> popup_numpad;
  lv_obj_t *active_range_control = nullptr;
  size_t active_macro = 0;
  // LVGL keeps this pointer for later layout passes
  std::vector<lv_coord_t> row_dsc;
};

#endif // __MACRO_PANEL_H__
