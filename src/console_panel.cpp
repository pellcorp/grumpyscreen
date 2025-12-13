#include "console_panel.h"
#include "state.h"
#include "logger.h"
#include "klipper_temp_filter.h"

#include <algorithm>
#include <cctype>

LV_IMG_DECLARE(delete_img);
LV_FONT_DECLARE(dejavusans_mono_14);

ConsolePanel::ConsolePanel(KWebSocketClient &websocket_client, std::mutex &lock, lv_obj_t *parent)
  : ws(websocket_client)
  , lv_lock(lock)
  , console_cont(lv_obj_create(parent))
  , top_cont(lv_obj_create(console_cont))
  , output(lv_textarea_create(top_cont))
  , delete_btn(top_cont, &delete_img, "", &ConsolePanel::_handle_delete_btn, this)
{
  lv_obj_align(console_cont, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_size(console_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(console_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(console_cont, 0, 0);
  lv_obj_set_style_text_font(console_cont, &dejavusans_mono_14, LV_STATE_DEFAULT);

  lv_obj_set_flex_grow(top_cont, 1);
  lv_obj_set_style_pad_all(top_cont, 0, 0);
  lv_obj_set_width(top_cont, LV_PCT(100));

  lv_obj_set_style_border_width(output, 0, 0);
  lv_obj_set_size(output, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_border_width(output, 0, LV_STATE_FOCUSED | LV_PART_CURSOR);
  lv_obj_align(delete_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);

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
