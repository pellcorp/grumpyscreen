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
  , lower_cont(create_row(console_cont))
  , bottom_cont(create_row(lower_cont))
  , input(lv_textarea_create(bottom_cont))
  , kb(lv_keyboard_create(lower_cont))
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

  // a gcode entry line under the log, beside the clear button; the keyboard
  // slides over the log while the line has focus and sends on enter. The entry
  // row and its keyboard share a box that is content-sized while the keyboard
  // is away and most of the tab while it is up; the log takes the rest.
  lv_obj_set_size(lower_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(lower_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_size(bottom_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(bottom_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bottom_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_size(input, 0, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(input, scale_r(34), 0);  // the same height as the clear tile beside it
  lv_obj_set_flex_grow(input, 1);
  lv_textarea_set_one_line(input, true);
  lv_textarea_set_placeholder_text(input, "Send gcode...");
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_width(kb, LV_PCT(100));
  lv_obj_set_flex_grow(kb, 1);
  lv_keyboard_set_textarea(kb, input);
  for (lv_event_code_t c : {LV_EVENT_FOCUSED, LV_EVENT_DEFOCUSED, LV_EVENT_READY, LV_EVENT_CANCEL}) {
    lv_obj_add_event_cb(input, &ConsolePanel::_handle_input, c, this);
  }
  // A finger on the keyboard's bottom row often lands a few pixels past it, on
  // the panel margin. Plain containers take click focus by default, which
  // defocused the entry and closed the keyboard without sending; the
  // scaffolding must not be a focus target. The log still is, so tapping it
  // dismisses the keyboard.
  for (lv_obj_t *o : {console_cont, top_cont, lower_cont, bottom_cont}) {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICK_FOCUSABLE);
  }
  // a theme without hairlines would run the log and the entry together: a
  // rule between them, in the raised grey the graph guides use
  if (border_w() == 0) {
    lv_obj_t *rule = create_row(console_cont);
    lv_obj_move_to_index(rule, lv_obj_get_index(lower_cont));
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

// The log keeps this many lines: five screens of scrollback on the small
// display, and every message added re-lays out the whole text, so the cap is
// also what each line costs. The seed asks moonraker for the same number.
static const int MAX_LINES = 50;

// thanks Chad
static void ta_limit_lines(lv_obj_t * ta) {
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
  if (line_count > MAX_LINES) {
    int drop = line_count - MAX_LINES;

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

void ta_add_text_limit_lines(lv_obj_t * ta, const std::string &line) {
  // Always append a newline at the end
  std::string msg = line + "\n";
  lv_textarea_add_text(ta, msg.c_str());
  ta_limit_lines(ta);
}

// moonraker responses carry mainsail's html (<span class=warning--text>), which
// a textarea would print literally
static std::string strip_tags(const std::string &s) {
  std::string out;
  bool in_tag = false;
  for (char c : s) {
    if (c == '<') in_tag = true;
    else if (c == '>') in_tag = false;
    else if (!in_tag) out += c;
  }
  return out;
}

// Seed the log the way mainsail does: moonraker's recent gcode store, which has
// the commands every client sent and klipper's answers, then the live stream
// carries on from there.
void ConsolePanel::foreground() {
  ws.send_jsonrpc("server.gcode_store", json{{"count", MAX_LINES}}, [this](json &d) {
    auto &store = d["/result/gcode_store"_json_pointer];
    if (store.is_null()) return;
    // one string, set once: adding line by line would lay the textarea out
    // once per entry
    std::string text;
    for (auto &e : store) {
      auto &m = e["message"];
      if (!m.is_string()) continue;
      const std::string &msg = m.get_ref<const std::string &>();
      if (klipper_is_temp_report(msg.c_str())) continue;
      text += (e["type"] == "command" ? "> " : "") + strip_tags(msg) + "\n";
    }
    std::lock_guard<std::mutex> lock(lv_lock);
    lv_textarea_set_text(output, text.c_str());
    ta_limit_lines(output);  // a multi-line answer can push the seed past the cap
  });
}

void ConsolePanel::handle_macro_response(json &j) {
  LOG_TRACE("console macro response {}", j.dump());

  if (j.contains("params")) {
    std::lock_guard<std::mutex> lock(lv_lock);
    for (auto &l : j["params"]) {
      std::string v = l.template get<std::string>();
      if (klipper_is_temp_report(v.c_str())) {
          // Ignore TEMPERATURE_WAIT spam
          return;
      }
      // ta_add_text_limit_lines ends the line itself
      ta_add_text_limit_lines(output, strip_tags(v));
    }
  }
}

void ConsolePanel::handle_input(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED) {
    // keys big enough for a finger: the lower box takes most of the tab
    lv_obj_set_height(lower_cont, LV_PCT(65));
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(output, LV_COORD_MAX, LV_ANIM_OFF);
    return;
  }
  if (code == LV_EVENT_READY) {
    const char *text = lv_textarea_get_text(input);
    if (text != NULL && text[0] != '\0') {
      // klipper does not echo commands, so show what was sent
      ta_add_text_limit_lines(output, std::string("> ") + text);
      ws.gcode_script(text);
      lv_textarea_set_text(input, "");
    }
  }
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_height(lower_cont, LV_SIZE_CONTENT);
  lv_obj_clear_state(input, LV_STATE_FOCUSED);
  // the indev only sends FOCUSED when a different object is pressed; forget
  // this one so the next tap on it focuses again
  lv_indev_reset(NULL, input);
}

void ConsolePanel::handle_delete_btn(lv_event_t *e) {
  lv_textarea_set_text(output, "");
}
