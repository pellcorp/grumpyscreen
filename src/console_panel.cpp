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
  , bottom_cont(create_row(console_cont))
  , input(lv_textarea_create(bottom_cont))
  , delete_btn(bottom_cont, Icons::DELETE_IMG, "", &ConsolePanel::_handle_delete_btn, this)
{
  lv_obj_set_flex_flow(console_cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_set_flex_grow(top_cont, 1);
  lv_obj_set_width(top_cont, LV_PCT(100));

  // The log is a textarea wearing the panel look over the theme's input look
  // (panel is added later, so its bg/border/radius/pad win): a log, not an
  // entry. The mono face is the one fixed-width font compiled in and has no
  // scaled sizes, so it is the one unscaled font in the UI, worn by the log only.
  lv_obj_add_style(output, &styles().panel, 0);
  lv_obj_set_style_text_font(output, &dejavusans_mono_14, 0);
  lv_obj_set_size(output, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_border_width(output, 0, LV_STATE_FOCUSED | LV_PART_CURSOR);

  // a gcode entry line under the log, beside the clear button. Layout only for
  // now: sending is a separate PR, so the line is disabled and takes no input.
  lv_obj_set_size(bottom_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(bottom_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bottom_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_size(input, 0, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(input, scale_r(34), 0);  // the same height as the clear tile beside it
  lv_obj_set_flex_grow(input, 1);
  lv_textarea_set_one_line(input, true);
  lv_textarea_set_placeholder_text(input, "Gcode input coming soon");
  lv_obj_add_state(input, LV_STATE_DISABLED);
  lv_obj_clear_flag(input, LV_OBJ_FLAG_CLICKABLE);
  // a theme without hairlines would run the log and the entry together: a
  // rule between them, in the raised grey the graph guides use. Its own row,
  // not a border on the entry: the disabled entry's grey filter would fade it.
  if (border_w() == 0) {
    lv_obj_t *rule = create_row(console_cont);
    lv_obj_move_to_index(rule, lv_obj_get_index(bottom_cont));
    lv_obj_set_size(rule, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(rule, col(RAISED), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  }

  // the clear button is a square icon-only tile the height of the entry line
  delete_btn.use_card();
  delete_btn.hide_label();
  lv_obj_set_size(delete_btn.get_container(), scale_r(34), scale_r(34));

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
