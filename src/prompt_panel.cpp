#include "prompt_panel.h"
#include "print_status_panel.h"
#include "state.h"
#include "utils.h"
#include "logger.h"
#include "theme.h"

using namespace Theme;

LV_FONT_DECLARE(materialdesign_font_18);
// the screws tilt icons fluidd uses: mdi rotate-right, rotate-left, check-circle
#define MDI_ROTATE_RIGHT "\xF3\xB0\x91\xA7"
#define MDI_ROTATE_LEFT "\xF3\xB0\x91\xA5"
#define MDI_CHECK_CIRCLE "\xF3\xB0\x97\xA0"

// a colour as label recolour wants it: "#RRGGBB text#"
static std::string hex(lv_color_t c) {
  return fmt::format("{:06X}", lv_color_to32(c) & 0xFFFFFF);
}

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

// klipper's own dialogs send no action:prompt, they are printer status the
// way fluidd and mainsail open them: manual_probe (PROBE_CALIBRATE,
// Z_ENDSTOP_CALIBRATE and the like), bed_screws (BED_SCREWS_ADJUST) and
// screws_tilt_adjust (SCREWS_TILT_CALCULATE)
void PromptPanel::consume(json &j) {
  auto &status = j["/params/0"_json_pointer];
  if (!status.is_object()) {
    return;
  }

  bool manual_probe = status.contains("manual_probe");
  bool bed_screws = status.contains("bed_screws");
  bool screws_tilt = status.contains("screws_tilt_adjust") && status["screws_tilt_adjust"].contains("results");
  if (!manual_probe && !bed_screws && !screws_tilt) {
    return;
  }

  // State consumes before us, so it holds the whole object, not just what changed
  State *state = State::get_instance();
  std::lock_guard<std::mutex> lock(lv_lock);
  if (manual_probe) {
    json mp = state->get_data("/printer_state/manual_probe"_json_pointer);
    update_manual_probe(mp);
  }
  if (bed_screws) {
    json bs = state->get_data("/printer_state/bed_screws"_json_pointer);
    update_bed_screws(bs);
  }
  if (screws_tilt) {
    show_screws_tilt(status["screws_tilt_adjust"]);
  }
}

// screwN_name from the printer config the way fluidd shows it ("front_left"
// -> "Front Left"), or a number when it is missing
static std::string screw_name(json &settings, int n) {
  auto &name = settings[fmt::format("screw{}_name", n)];
  if (!name.is_string() || name.template get<std::string>().empty()) {
    return fmt::format("Screw {}", n);
  }
  std::string pretty;
  bool word_start = true;
  for (char c : name.template get<std::string>()) {
    if (c == '_' || c == ' ') {
      word_start = true;
      continue;
    }
    if (word_start && !pretty.empty()) {
      pretty += ' ';
    }
    pretty += word_start ? (char)toupper(c) : c;
    word_start = false;
  }
  return pretty;
}

// like fluidd, a klipper dialog does not open by itself mid-print
static bool printing() {
  json state = State::get_instance()->get_data("/printer_state/print_stats/state"_json_pointer);
  return state.is_string() && state.template get<std::string>() == "printing";
}

// a column that scrolls when its rows do not fit
static lv_obj_t *create_table(lv_obj_t *parent) {
  lv_obj_t *table = create_row(parent);
  lv_obj_set_size(table, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(table, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(table, LV_OBJ_FLAG_SCROLLABLE);
  return table;
}

static lv_obj_t *create_table_row(lv_obj_t *table) {
  lv_obj_t *row = create_row(table);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}

// a label that takes an equal share of its row, centred
static lv_obj_t *create_cell(lv_obj_t *row, const char *text) {
  lv_obj_t *cell = lv_label_create(row);
  lv_obj_set_flex_grow(cell, 1);
  lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_recolor(cell, true);
  lv_label_set_text(cell, text);
  return cell;
}

// fluidd's z field: its label over the value, empty until there is one
static std::string z_field(const char *label, json &z) {
  return fmt::format("#{} {}#\n{}", hex(col(TEXT_DIM)), label,
                     z.is_number() ? fmt::format("{:.3f}", z.template get<double>()) : "");
}

void PromptPanel::update_manual_probe(json &mp) {
  bool active = mp.is_object() && mp.value("is_active", false);
  if (!active) {
    if (klipper_dialog == KlipperDialog::MANUAL_PROBE) {
      close();
    }
    return;
  }

  if (klipper_dialog != KlipperDialog::MANUAL_PROBE) {
    if (printing()) {
      return;
    }
    open(KlipperDialog::MANUAL_PROBE, "Manual Probe");
    lv_obj_t *table = create_table(flex);

    lv_obj_t *z_row = create_table_row(table);
    z_min_label = create_cell(z_row, "");
    info_label = create_cell(z_row, "");
    z_max_label = create_cell(z_row, "");

    // klipper's bisect steps, between the heights already tried
    lv_obj_t *bisect = create_table_row(table);
    add_button(bisect, "--", "TESTZ Z=--", "primary");
    add_button(bisect, "-", "TESTZ Z=-", "primary");
    lv_obj_set_flex_grow(create_cell(bisect, ""), 2);
    add_button(bisect, "+", "TESTZ Z=+", "primary");
    add_button(bisect, "++", "TESTZ Z=++", "primary");

    // fluidd's offsets: 1, 0.1 and its default z adjust distances
    for (const char *offset : {"1", "0.1", "0.05", "0.025", "0.01", "0.005"}) {
      lv_obj_t *row = create_table_row(table);
      add_button(row, LV_SYMBOL_MINUS, fmt::format("TESTZ Z=-{}", offset), "primary");
      create_cell(row, offset);
      add_button(row, LV_SYMBOL_PLUS, fmt::format("TESTZ Z=+{}", offset), "primary");
    }

    add_button(footer_cont, "Abort", "ABORT", "none");
    add_button(footer_cont, "Accept", "ACCEPT", "primary");
  }

  lv_label_set_text(z_min_label, z_field("Z Min", mp["z_position_lower"]).c_str());
  lv_label_set_text(info_label, z_field("Z", mp["z_position"]).c_str());
  lv_label_set_text(z_max_label, z_field("Z Max", mp["z_position_upper"]).c_str());
}

void PromptPanel::update_bed_screws(json &bs) {
  bool active = bs.is_object() && bs.value("is_active", false);
  if (!active) {
    if (klipper_dialog == KlipperDialog::BED_SCREWS) {
      close();
    }
    return;
  }

  if (klipper_dialog != KlipperDialog::BED_SCREWS) {
    if (printing()) {
      return;
    }
    open(KlipperDialog::BED_SCREWS, "Bed Screws Adjust");
    info_label = add_text("");
    lv_obj_t *msg = add_text("Click Adjusted if a significant adjustment is necessary on the current screw; "
                             "otherwise, click Accept to continue.");
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    progress_bar = lv_bar_create(flex);
    lv_obj_set_width(progress_bar, lv_pct(100));
    add_button(footer_cont, "Abort", "ABORT", "warning");
    add_button(footer_cont, "Adjusted", "ADJUSTED", "primary");
    add_button(footer_cont, "Accept", "ACCEPT", "primary");
  }

  json settings = State::get_instance()->get_data("/printer_state/configfile/settings/bed_screws"_json_pointer);
  int screws = 0;
  while (settings.is_object() && settings.contains(fmt::format("screw{}", screws + 1))) {
    screws++;
  }
  int current = bs.value("current_screw", 0);
  int accepted = bs.value("accepted_screws", 0);
  lv_label_set_text(info_label, fmt::format("Screw name: {}\nScrew index: {} of {}\nAccepted screws: {} of {}",
                                            screw_name(settings, current + 1),
                                            current + 1, screws, accepted, screws).c_str());
  lv_bar_set_range(progress_bar, 0, screws > 0 ? screws : 1);
  lv_bar_set_value(progress_bar, accepted, LV_ANIM_OFF);
}

void PromptPanel::show_screws_tilt(json &st) {
  // MAX_DEVIATION makes it a check (a print start), not something to read off
  json max_deviation = State::get_instance()->get_data("/printer_state/screws_tilt_adjust/max_deviation"_json_pointer);
  auto &results = st["results"];
  if (!max_deviation.is_null() || !results.is_object() || results.empty() || printing()) {
    return;
  }

  json settings = State::get_instance()->get_data("/printer_state/configfile/settings/screws_tilt_adjust"_json_pointer);
  double base_z = 0;
  for (auto &r : results) {
    if (r.value("is_base", false)) {
      base_z = r.value("z", 0.0);
    }
  }

  open(KlipperDialog::SCREWS_TILT, "Screws Tilt Adjust");

  // fluidd's table: name over X/Y, Z under how far it is off the base, and the
  // turn with fluidd's icons, green under 6 minutes
  lv_obj_t *table = create_table(flex);
  for (int n = 1; results.contains(fmt::format("screw{}", n)); n++) {
    auto &r = results[fmt::format("screw{}", n)];
    auto &xy = settings[fmt::format("screw{}", n)];
    double z = r.value("z", 0.0);

    lv_obj_t *row = create_table_row(table);

    lv_obj_t *name = lv_label_create(row);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_set_style_text_font(name, scale_font(16), 0);
    lv_label_set_recolor(name, true);
    lv_label_set_text(name, fmt::format("{}\n#{} X = {}, Y = {}#", screw_name(settings, n),
                                        hex(col(TEXT_DIM)),
                                        xy.is_array() && xy.size() > 1 ? xy[0].template get<double>() : 0.0,
                                        xy.is_array() && xy.size() > 1 ? xy[1].template get<double>() : 0.0).c_str());

    lv_obj_t *zl = lv_label_create(row);
    lv_obj_set_style_text_font(zl, scale_font(16), 0);
    lv_obj_set_style_text_align(zl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_pad_hor(zl, gap() * 2, 0);
    lv_label_set_recolor(zl, true);
    lv_label_set_text(zl, fmt::format("#{} ({}{:.4f})#\n{:.4f}", hex(col(TEXT_DIM)),
                                      z - base_z < 0 ? "" : "+", z - base_z, z).c_str());

    lv_obj_t *chip = create_row(row);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(chip, gap(), 0);
    lv_obj_set_style_radius(chip, gap(), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);

    std::string adjust = r.value("adjust", "00:00");
    if (r.value("is_base", false)) {
      lv_obj_set_style_bg_color(chip, col(RAISED), 0);
    } else {
      int minutes = std::stoi(adjust.substr(0, 2)) * 60 + std::stoi(adjust.substr(3, 2));
      lv_obj_set_style_bg_color(chip, minutes < 6 ? lv_palette_main(LV_PALETTE_GREEN) : col(DANGER), 0);
      lv_obj_t *icon = lv_label_create(chip);
      lv_obj_set_style_text_font(icon, &materialdesign_font_18, 0);
      lv_label_set_text(icon, minutes == 0 ? MDI_CHECK_CIRCLE
                              : r.value("sign", "") == "CW" ? MDI_ROTATE_RIGHT : MDI_ROTATE_LEFT);
    }
    lv_obj_t *adj = lv_label_create(chip);
    lv_obj_set_style_text_font(adj, scale_font(16), 0);
    lv_label_set_text(adj, r.value("is_base", false) ? "base" : adjust.c_str());
  }

  add_button(footer_cont, "Retry", "SCREWS_TILT_CALCULATE", "none");
  add_button(footer_cont, "Accept", "", "primary");
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

void PromptPanel::open(KlipperDialog d, const std::string &title) {
  if (klipper_dialog == KlipperDialog::NONE) {
    restore_print_status_foreground_ = print_status_panel.is_foreground();
  }
  clear();
  klipper_dialog = d;
  lv_label_set_text(header, title.c_str());
  foreground();
}

void PromptPanel::clear() {
  lv_obj_clean(footer_cont);
  lv_obj_clean(flex);
  info_label = NULL;
  z_min_label = NULL;
  z_max_label = NULL;
  progress_bar = NULL;
  klipper_dialog = KlipperDialog::NONE;
}

void PromptPanel::close() {
  background();

  if (restore_print_status_foreground_) {
    print_status_panel.foreground();
  }
  restore_print_status_foreground_ = false;

  clear();
}

lv_obj_t *PromptPanel::add_text(const std::string &text) {
  lv_obj_t *textfield = lv_label_create(flex);
  lv_obj_set_size(textfield, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(textfield, touch_h(), 0);
  lv_label_set_long_mode(textfield, LV_LABEL_LONG_WRAP);
  lv_obj_set_flex_grow(textfield, 1);
  lv_label_set_text(textfield, text.c_str());
#ifdef DEBUG_LINES
  lv_obj_set_style_border_width(textfield, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(textfield, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(textfield, lv_palette_lighten(LV_PALETTE_GREEN, 2), LV_PART_MAIN | LV_STATE_DEFAULT);
#endif
  return textfield;
}

void PromptPanel::add_button(lv_obj_t *parent, const std::string &text, const std::string &cmd, const std::string &type) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, lv_pct(45), touch_h());
  lv_obj_set_style_max_width(btn, lv_pct(45), 0);
  lv_obj_set_style_min_width(btn, scale_w(32), 0);
  lv_obj_set_flex_grow(btn, 1);
  lv_obj_t *label = lv_label_create(btn);
  // a hidden label is abused to transfer the command and auto-clean it
  lv_obj_t *command = lv_label_create(btn);
  lv_obj_set_size(command, 1, 1);
  lv_obj_add_flag(command, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(label, text.c_str());
  lv_label_set_text(command, cmd.c_str());
  lv_obj_center(label);
  colour_prompt_btn(btn, type);
  lv_obj_add_event_cb(btn, _handle_callback, LV_EVENT_CLICKED, this);
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
    if (klipper_dialog == KlipperDialog::SCREWS_TILT) {
      // klipper never ends the results, the dialog goes on any button; not
      // from inside this button's own click
      lv_async_call([](void *panel) { ((PromptPanel *)panel)->close(); }, this);
    }
    if (!cmd.empty()) {
      ws.gcode_script(cmd);
    }
  }
}

void PromptPanel::handle_macro_response(json &j) {
  if (get_log_level() <= LogLevel::TRACE) {
    LOG_TRACE("macro response: {}", j.dump());
  }
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
        clear();

        // set header here
        lv_label_set_text(header, prompt_header.c_str());
      } else if (command.find("prompt_text") == 0) {
        std::string prompt_text = command.substr(12);
        LOG_DEBUG("PROMPT_TEXT: {}", prompt_text);
        add_text(prompt_text);
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
        lv_obj_t *parent;
        if (command.find("prompt_footer_button") == 0) {
          parent = footer_cont;
        } else if (button_group_cont == NULL) {
          parent = flex;
        } else {
          parent = button_group_cont;
        }
        add_button(parent, prompt_footer_button, prompt_button_command, prompt_button_type);
      } else if (command.find("prompt_show") == 0) {
        LOG_DEBUG("PROMPT_SHOW");
        restore_print_status_foreground_ = print_status_panel.is_foreground();
        foreground();
      } else if (command.find("prompt_end") == 0) {
        LOG_DEBUG("PROMPT_END");
        close();
      } else {
        LOG_DEBUG("action {} --- not supported", command);
      }
    }
  }
}
