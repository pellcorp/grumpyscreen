#include "finetune_panel.h"
#include "state.h"
#include "logger.h"
#include "config.h"
#include "icons.h"
#include "theme.h"

#include <algorithm>


using namespace Theme;

namespace {

// "{:.5}" can come out in exponent form for a tiny offset; show that as zero
std::string fmt_offset(double v, const char *unit) {
  std::string s = fmt::format("{:.5} {}", v, unit);
  return s.find_first_of("eE") == std::string::npos ? s : fmt::format("0.0 {}", unit);
}

std::string fmt_pct(double factor) {
  return fmt::format("{}%", static_cast<int>(factor * 100));
}

}  // namespace

FineTunePanel::FineTunePanel(KWebSocketClient &websocket_client, std::mutex &l)
  : NotifyConsumer(l)
  , ws(websocket_client)
  , panel_cont(create_screen(NULL))
  , step_selector(panel_cont, "Z / PA step (mm)", {"0.01", "0.05", "0.10", ""}, 0,
                  &FineTunePanel::_handle_selector, this)
  , multiplier_selector(panel_cont, "Speed / Flow step (%)", {"1", "5", "10", "25", ""}, 0,
                        &FineTunePanel::_handle_selector, this)
  , back_btn(panel_cont, Icons::BACK, "Back", &FineTunePanel::_handle_back, this)
{
  lv_obj_move_background(panel_cont);

  // readout | down | up | reset, four rows sharing the height above a
  // content-sized selector row
  static lv_coord_t col_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                 LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                 LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(panel_cont, col_dsc, row_dsc);

  make_row(z,     0, Icons::HOME_Z,       "Z offset",         "0.0 mm");
  make_row(pa,    1, Icons::PA_PLUS_IMG,  "Pressure adv.",    "0.0 mm/s");
  make_row(speed, 2, Icons::SPEED_UP_IMG, "Speed",            "100%");
  make_row(flow,  3, Icons::FLOW_UP_IMG,  "Flow",             "100%");

  // the selectors and Back share the content-sized bottom row: it takes the
  // height a row of keys needs, the four parameter rows above get the rest,
  // and all three sit at the end of it, a screen margin from the bottom edge
  for (Selector *s : {&multiplier_selector, &step_selector}) s->seat_at_row_bottom();
  lv_obj_set_grid_cell(multiplier_selector.get_container(), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_END, 4, 1);
  lv_obj_set_grid_cell(step_selector.get_container(), LV_GRID_ALIGN_STRETCH, 1, 2, LV_GRID_ALIGN_END, 4, 1);
  back_btn.use_card();
  lv_obj_set_grid_cell(back_btn.get_container(), LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_END, 4, 1);
  back_btn.match_height(panel_cont, multiplier_selector.get_container());

  ws.register_notify_update(this);
}

FineTunePanel::~FineTunePanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
  ws.unregister_notify_update(this);
}

void FineTunePanel::make_row(Row &r, int grid_row, const void *icon, const char *name,
                             const char *initial) {
  // the readout card: icon, dim name, value
  r.card = lv_obj_create(panel_cont);
  lv_obj_add_style(r.card, &styles().card, 0);
  lv_obj_set_style_border_width(r.card, 0, 0);  // a readout: room, but no frame
  lv_obj_set_style_pad_hor(r.card, gap(), 0);
  lv_obj_clear_flag(r.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(r.card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r.card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r.card, gap(), 0);
  lv_obj_set_grid_cell(r.card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, grid_row, 1);

  lv_obj_t *img = lv_img_create(r.card);
  lv_img_set_src(img, icon);
  lv_obj_add_event_cb(r.card, fit_first_icon, LV_EVENT_SIZE_CHANGED, NULL);

  lv_obj_t *label = lv_label_create(r.card);
  lv_label_set_text(label, name);
  lv_obj_add_style(label, &styles().dim_label, 0);
  // one line, dotted if it must be: a content-height label would wrap instead
  lv_obj_set_height(label, lv_font_get_line_height(lv_obj_get_style_text_font(label, 0)));
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(label, 1);

  r.value = lv_label_create(r.card);
  lv_label_set_text(r.value, initial);
  lv_obj_set_style_text_font(r.value, scale_font(14), 0);

  // its three actions: plain text buttons, one grid cell each
  r.down  = create_flat_btn(panel_cont, "-", &FineTunePanel::_handle_btn, this);
  r.up    = create_flat_btn(panel_cont, "+", &FineTunePanel::_handle_btn, this);
  r.reset = create_flat_btn(panel_cont, "Reset", &FineTunePanel::_handle_btn, this);
  int col = 1;
  for (lv_obj_t *btn : {r.down, r.up, r.reset}) {
    lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, col++, 1, LV_GRID_ALIGN_STRETCH, grid_row, 1);
  }
  // the signs read better a size up
  for (lv_obj_t *btn : {r.down, r.up}) {
    lv_obj_set_style_text_font(lv_obj_get_child(btn, 0), scale_font(18), 0);
  }
}

void FineTunePanel::foreground() {
  update_values(State::get_instance()->get_data("/printer_state"_json_pointer));
  lv_obj_move_foreground(panel_cont);
}

void FineTunePanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  update_values(j["/params/0"_json_pointer]);
}

// the same four fields whether they come from the full state or an update;
// a label is only rewritten (and redrawn) when its text actually changes
void FineTunePanel::update_values(const json &state) {
  auto set = [](lv_obj_t *label, const json &v, std::string (*fmt)(double)) {
    if (v.is_null()) return;
    const std::string s = fmt(v.template get<double>());
    if (s != lv_label_get_text(label)) lv_label_set_text(label, s.c_str());
  };
  set(z.value, state.value("/gcode_move/homing_origin/2"_json_pointer, json()),
      [](double v) { return fmt_offset(v, "mm"); });
  set(pa.value, state.value("/extruder/pressure_advance"_json_pointer, json()),
      [](double v) { return fmt_offset(v, "mm/s"); });
  set(speed.value, state.value("/gcode_move/speed_factor"_json_pointer, json()), fmt_pct);
  set(flow.value, state.value("/gcode_move/extrude_factor"_json_pointer, json()), fmt_pct);
}

void FineTunePanel::handle_selector(lv_event_t *e) {
  lv_obj_t *selector = lv_event_get_target(e);
  uint32_t idx = lv_btnmatrix_get_selected_btn(selector);
  for (Selector *s : {&step_selector, &multiplier_selector}) {
    if (selector == s->get_selector()) s->set_selected_idx(idx);
  }
}

void FineTunePanel::handle_btn(lv_obj_t *btn) {
  State *state = State::get_instance();
  const char *step = lv_btnmatrix_get_btn_text(step_selector.get_selector(), step_selector.get_selected_idx());
  const char *mult = lv_btnmatrix_get_btn_text(multiplier_selector.get_selector(),
                                               multiplier_selector.get_selected_idx());

  if (btn == z.reset) {
    ws.gcode_script("SET_GCODE_OFFSET Z=0 MOVE=1");
  } else if (btn == z.up || btn == z.down) {
    ws.gcode_script(fmt::format("SET_GCODE_OFFSET Z_ADJUST={}{} MOVE=1", btn == z.up ? "+" : "-", step));

  } else if (btn == pa.reset) {
    auto v = state->get_data("/printer_state/configfile/settings/extruder/pressure_advance"_json_pointer);
    if (!v.is_null()) ws.gcode_script(fmt::format("SET_PRESSURE_ADVANCE ADVANCE={}", v.template get<double>()));
  } else if (btn == pa.up || btn == pa.down) {
    auto cur = state->get_data("/printer_state/extruder/pressure_advance"_json_pointer);
    if (!cur.is_null()) {
      const double d = btn == pa.up ? std::stod(step) : -std::stod(step);
      ws.gcode_script(fmt::format("SET_PRESSURE_ADVANCE ADVANCE={}", std::max(0.0, cur.template get<double>() + d)));
    }

  } else if (btn == speed.reset) {
    ws.gcode_script("M220 S100");
  } else if (btn == speed.up || btn == speed.down) {
    auto cur = state->get_data("/printer_state/gcode_move/speed_factor"_json_pointer);
    if (!cur.is_null()) {
      const int d = btn == speed.up ? std::stoi(mult) : -std::stoi(mult);
      ws.gcode_script(fmt::format("M220 S{}", std::max(1, static_cast<int>(cur.template get<double>() * 100) + d)));
    }

  } else if (btn == flow.reset) {
    ws.gcode_script("M221 S100");
  } else if (btn == flow.up || btn == flow.down) {
    auto cur = state->get_data("/printer_state/gcode_move/extrude_factor"_json_pointer);
    if (!cur.is_null()) {
      const int d = btn == flow.up ? std::stoi(mult) : -std::stoi(mult);
      ws.gcode_script(fmt::format("M221 S{}", std::max(1, static_cast<int>(cur.template get<double>() * 100) + d)));
    }
  }
}
