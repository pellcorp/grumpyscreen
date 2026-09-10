#include "prompt_panel.h"
#include "print_status_panel.h"
#include "state.h"
#include "utils.h"
#include "logger.h"
#include "theme.h"

using namespace Theme;

// uncomment for helper boxes
// #define DEBUG_LINES

// a row of prompt buttons, spread evenly; the footer and every button group
static lv_obj_t *button_row(lv_obj_t *parent) {
  lv_obj_t *row = create_row(parent);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}

// Klipper names a prompt button secondary/info/primary/warning/error, which
// are the theme roles under other names -- so a danger_colour set in the
// config turns up on a klipper prompt too, without this panel ever knowing
// what colour it is. A default button keeps the flat button look.
static void colour_prompt_btn(lv_obj_t *btn, const std::string &type) {
  lv_color_t c;
  if (type == "primary") c = theme_primary();
  else if (type == "secondary") c = theme_secondary();
  else if (type == "info" || type == "warning") c = col(WARNING);
  else if (type == "error") c = col(DANGER);
  else return;
  lv_obj_set_style_bg_color(btn, c, 0);
  lv_obj_set_style_text_color(btn, col(ON_PRIMARY), 0);
}

PromptPanel::PromptPanel(KWebSocketClient &websocket_client, std::mutex &lock, lv_obj_t *parent, PrintStatusPanel &print_status_panel)
    : NotifyConsumer(lock)
    , ws(websocket_client)
    , print_status_panel(print_status_panel)
    , prompt_cont(create_screen(lv_scr_act()))
    , flex(create_row(prompt_cont))
    , header(lv_label_create(prompt_cont))
    , footer_cont(button_row(prompt_cont))
{
  // header, body, buttons: the one-line header and the button row take what
  // they need, the body gets the rest
  static lv_coord_t grid_main_row_dsc_detail[] = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc_detail[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(prompt_cont, grid_main_col_dsc_detail, grid_main_row_dsc_detail);

  lv_obj_set_grid_cell(header,      LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_START,   0, 1);
  lv_obj_set_grid_cell(flex,        LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(footer_cont, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_END,     2, 1);

  lv_obj_set_style_text_font(header, scale_font(16), 0);

  lv_obj_set_flex_flow(flex, LV_FLEX_FLOW_COLUMN_WRAP);
  lv_obj_set_flex_align(flex, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

#ifdef DEBUG_LINES
  // for debugging
  lv_obj_set_style_border_width(header, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(header, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(flex, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(flex, lv_palette_main(LV_PALETTE_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(footer_cont, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(footer_cont, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
#endif

  ws.register_notify_update(this);
  ws.register_method_callback("notify_gcode_response", "MainPanel",[this](json& d) { this->handle_macro_response(d); });

  // create header
  lv_label_set_text(header, "HEADER");

  background(); // hide ourselves
}

void PromptPanel::consume(json &j) {
}

PromptPanel::~PromptPanel() {
  if (prompt_cont != NULL) {
    lv_obj_del(prompt_cont);
    prompt_cont = NULL;
  }

  ws.unregister_notify_update(this);
}

void PromptPanel::foreground() {
  lv_obj_move_foreground(prompt_cont);
}

void PromptPanel::background() {
  lv_obj_move_background(prompt_cont);
}

void PromptPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);

  lv_obj_t *label = lv_obj_get_child(btn, 0);
  lv_obj_t *command = lv_obj_get_child(btn, 1);
  // check if btn in command map
  LOG_DEBUG("handle event");

  if (btn == NULL) {
    LOG_DEBUG("no button found");
  }

  if (label != NULL) {
    LOG_DEBUG("button: {}", lv_label_get_text(label));
  }

  if (command != NULL) {
    std::string cmd = lv_label_get_text(command);
    LOG_DEBUG("button: {}", cmd);
    ws.gcode_script(cmd);
  }
}

void PromptPanel::handle_macro_response(json &j) {
  LOG_TRACE("macro response: {}", j.dump());
  auto &v = j["/params/0"_json_pointer];

  if (!v.is_null()) {
    LOG_TRACE("data found");
    std::string resp = v.template get<std::string>();
    std::lock_guard<std::mutex> lock(lv_lock);
    LOG_TRACE("data: {}", resp);

    if (resp.find("// action:", 0) == 0) {
      // it is an action
      std::string command = resp.substr(10);
      LOG_DEBUG("action: {}", command);

      if (command.find("prompt_begin") == 0) {
        restore_print_status_foreground_ = false;
        std::string prompt_header = command.substr(13);
        LOG_DEBUG("PROMPT_BEGIN: {}", prompt_header);

        // remove buttons
        lv_obj_clean(footer_cont);
        lv_obj_clean(flex);

        // set header here
        lv_label_set_text(header, prompt_header.c_str());
      } else if (command.find("prompt_text") == 0) {
        std::string prompt_text = command.substr(12);
        LOG_DEBUG("PROMPT_TEXT: {}", prompt_text);
        // create label and add to flex field
        lv_obj_t *textfield = lv_label_create(flex);
        lv_obj_set_size(textfield, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(textfield, touch_h(), 0);
        lv_label_set_long_mode(textfield, LV_LABEL_LONG_WRAP);
        lv_obj_set_flex_grow(textfield, 1);
        lv_label_set_text(textfield, prompt_text.c_str());
#ifdef DEBUG_LINES
        lv_obj_set_style_border_width(textfield, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(textfield, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(textfield, lv_palette_lighten(LV_PALETTE_GREEN, 2), LV_PART_MAIN | LV_STATE_DEFAULT);
#endif
      // due to using find, order IS important!
      } else if (command.find("prompt_button_group_start") == 0) {
        LOG_DEBUG("Button group created");
        // create new button group in flex window and mark active
        button_group_cont = button_row(flex);
        lv_obj_set_flex_grow(button_group_cont, 1);
        lv_obj_set_style_max_height(button_group_cont, lv_pct(62), 0);
        lv_obj_set_style_min_height(button_group_cont, touch_h(), 0);

#ifdef DEBUG_LINES
        lv_obj_set_style_border_width(button_group_cont, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(button_group_cont, lv_palette_main(LV_PALETTE_PINK), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(button_group_cont, lv_palette_lighten(LV_PALETTE_PINK, 2), LV_PART_MAIN | LV_STATE_DEFAULT);
#endif
        // lv_obj_set_style_min_height(button_group_cont, lv_pct(5), 0);
      } else if (command.find("prompt_button_group_end") == 0) {
        // does nothing since start creates a new one
        LOG_DEBUG("Button group ended");
        button_group_cont = NULL;
      } else if (command.find("prompt_footer_button") == 0 || command.find("prompt_button") == 0) {
        int index_label = command.find("button", 0) + strlen("button");
        int index_first = command.find("|", index_label);
        int index_second = command.find("|", index_first + 1);
        LOG_DEBUG("indexes: {} {} {}", index_label, index_first, index_second);
        std::string prompt_footer_button = command.substr(index_label, index_first - index_label);
        std::string prompt_button_command;
        std::string prompt_button_type = "none";
        LOG_DEBUG("button: {} |  {} | {}", prompt_footer_button, prompt_button_command, prompt_button_type);
        if (index_second > 0) {
          prompt_button_command = command.substr(index_first + 1, index_second - index_first - 1);
          prompt_button_type = command.substr(index_second + 1, command.length() - index_second - 1);
        } else {
          prompt_button_command = command.substr(index_first + 1);
        }
        LOG_DEBUG("PROMPT_FOOTER_BUTTON: {} CMD: {}, type {}", prompt_footer_button, prompt_button_command, prompt_button_type);
        lv_obj_t *btn = NULL;
        if (command.find("prompt_footer_button") == 0) {
          btn = lv_btn_create(footer_cont);
        } else {
          if (button_group_cont == NULL) {
            btn = lv_btn_create(flex);
          } else {
            btn = lv_btn_create(button_group_cont);
          }
        }
        if (btn) {
          lv_obj_set_size(btn, lv_pct(45), touch_h());
          lv_obj_set_style_max_width(btn, lv_pct(45), 0);
          lv_obj_set_style_min_width(btn, scale_w(32), 0);
          lv_obj_set_flex_grow(btn, 1);
          lv_obj_t *label = lv_label_create(btn);
          // a hidden label is abused to transfer the command and auto-clean it
          lv_obj_t *command = lv_label_create(btn);
          lv_obj_set_size(command, 1, 1);
          lv_obj_add_flag(command, LV_OBJ_FLAG_HIDDEN);
          lv_label_set_text(label, prompt_footer_button.c_str());
          lv_label_set_text(command, prompt_button_command.c_str());
          lv_obj_center(label);
          colour_prompt_btn(btn, prompt_button_type);
          lv_obj_add_event_cb(btn, _handle_callback, LV_EVENT_CLICKED, this);
        }
      } else if (command.find("prompt_show") == 0) {
        LOG_DEBUG("PROMPT_SHOW");
        restore_print_status_foreground_ = print_status_panel.is_foreground();
        foreground();
      } else if (command.find("prompt_end") == 0) {
        LOG_DEBUG("PROMPT_END");
        background();

        if (restore_print_status_foreground_) {
          print_status_panel.foreground();
        }
        restore_print_status_foreground_ = false;

        // remove buttons
        lv_obj_clean(footer_cont);
        lv_obj_clean(flex);
      } else {
        LOG_DEBUG("action {} --- not supported", command);
      }
    }
  }
}
