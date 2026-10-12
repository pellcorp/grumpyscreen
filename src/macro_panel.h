#ifndef __MACRO_PANEL_H__
#define __MACRO_PANEL_H__

#include "button_container.h"
#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <memory>
#include <string>
#include <vector>

// One button per [macro "GCODE"] section in grumpyscreen.cfg
class MacroPanel {
 public:
  MacroPanel(KWebSocketClient &ws, lv_obj_t *parent);

 private:
  static void handle_click(lv_event_t *event);

  KWebSocketClient &ws;
  lv_obj_t *grid;
  std::vector<std::unique_ptr<ButtonContainer>> buttons;
  std::vector<std::string> gcodes;
  // LVGL keeps this pointer for later layout passes
  std::vector<lv_coord_t> row_dsc;
};

#endif // __MACRO_PANEL_H__
