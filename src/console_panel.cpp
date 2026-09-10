#include "console_panel.h"
#include "state.h"
#include "logger.h"
#include "klipper_temp_filter.h"
#include "icons.h"
#include "theme.h"

using namespace Theme;

#include <algorithm>
#include <cctype>

LV_FONT_DECLARE(dejavusans_mono_14);

ConsolePanel::ConsolePanel(KWebSocketClient &websocket_client, std::mutex &lock, lv_obj_t *parent)
  : ws(websocket_client)
  , lv_lock(lock)
  , console_cont(create_screen(parent))
  , top_cont(create_row(console_cont))
  , output(lv_textarea_create(top_cont))
  , delete_btn(top_cont, Icons::DELETE_IMG, "", &ConsolePanel::_handle_delete_btn, this)
{
  lv_obj_set_flex_flow(console_cont, LV_FLEX_FLOW_COLUMN);

  // the log takes the row and the clear tile sits beside it, bottom aligned:
  // the log's own area stops where the tile starts rather than running under it
  lv_obj_set_flex_grow(top_cont, 1);
  lv_obj_set_width(top_cont, LV_PCT(100));
  lv_obj_set_flex_flow(top_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  // The log is a textarea wearing the panel look over the theme's input look
  // (panel is added later, so its bg/border/radius/pad win): a log, not an
  // entry. The mono face is the one fixed-width font compiled in and has no
  // scaled sizes, so it is the one unscaled font in the UI, worn by the log only.
  lv_obj_add_style(output, &styles().panel, 0);
  lv_obj_set_style_text_font(output, &dejavusans_mono_14, 0);
  lv_obj_set_size(output, 0, LV_PCT(100));
  lv_obj_set_flex_grow(output, 1);
  lv_obj_set_style_border_width(output, 0, LV_STATE_FOCUSED | LV_PART_CURSOR);

  // an icon-only tile in the corner the Back tile takes on every other panel,
  // a little smaller than one: it is the only control on the screen
  delete_btn.use_card();
  delete_btn.hide_label();
  lv_obj_set_size(delete_btn.get_container(), scale_r(52), scale_r(52));

  ws.register_method_callback("notify_gcode_response",
			      "ConsolePanel",
			      [this](json& d) { this->handle_macro_response(d); });
}

ConsolePanel::~ConsolePanel() {
  if (console_cont != NULL) {
    lv_obj_del(console_cont);
    console_cont = NULL;
  }
}

lv_obj_t *ConsolePanel::get_container() {
  return console_cont;
}

// thanks Chad
void ta_add_text_limit_lines(lv_obj_t * ta, const std::string &line) {
  // Always append a newline at the end
  std::string msg = line + "\n";
  lv_textarea_add_text(ta, msg.c_str());

  // Get the full text after appending
  const char * full = lv_textarea_get_text(ta);

  // Count lines
  int line_count = 0;
  const char *p = full;
  while (*p) {
    if(*p == '\n') {
      line_count++;
    }
    p++;
  }

  if (p != full && *(p-1) != '\n') {
    line_count++;
  }

  // Trim if too many lines
  if (line_count > 100) {
    int drop = line_count - 100;

    // Find pointer to first line we want to keep
    const char * keep = full;
    while(drop > 0 && *keep) {
        if(*keep == '\n') drop--;
        keep++;
    }

    size_t keep_len = strlen(keep);
    std::string trimmed_text(keep, keep_len);
    lv_textarea_set_text(ta, trimmed_text.c_str());
  }

  // Auto-scroll to bottom
  lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
}

void ConsolePanel::handle_macro_response(json &j) {
  LOG_TRACE("console macro response {}", j.dump());

  if (j.contains("params")) {
    std::lock_guard<std::mutex> lock(lv_lock);
    for (auto &l : j["params"]) {
      std::string v = l.template get<std::string>() + "\n";
      if (klipper_is_temp_report(v.c_str())) {
          // Ignore TEMPERATURE_WAIT spam
          return;
      }
      ta_add_text_limit_lines(output, v);
    }
  }
}

void ConsolePanel::handle_delete_btn(lv_event_t *e) {
  lv_textarea_set_text(output, "");
}
