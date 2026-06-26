#include "exclude_object_panel.h"

#include "logger.h"
#include "simple_dialog.h"
#include "state.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

LV_IMG_DECLARE(back);

namespace {
  static const lv_coord_t MARGIN = 12;

  lv_coord_t calc_canvas_dim() {
    lv_coord_t screen_w = lv_disp_get_physical_hor_res(NULL);
    lv_coord_t screen_h = lv_disp_get_physical_ver_res(NULL);
    lv_coord_t half_screen = screen_w / 2;
    lv_coord_t max_height = screen_h - 32;
    return std::min(half_screen, max_height);
  }

  void handle_exclude_dialog_result(lv_obj_t *, uint32_t button_idx, void *user_data) {
    auto *panel = static_cast<ExcludeObjectPanel *>(user_data);
    panel->handle_dialog_result(button_idx);
  }

  bool parse_point2(const json &v, double &x, double &y) {
    if (v.is_array() && v.size() >= 2) {
      x = v[0].template get<double>();
      y = v[1].template get<double>();
      return true;
    }

    if (v.is_string()) {
      std::string s = v.template get<std::string>();
      std::replace(s.begin(), s.end(), ',', ' ');
      std::stringstream ss(s);
      if (ss >> x >> y) {
        return true;
      }
    }

    return false;
  }

  bool parse_scalar(const json &v, double &out) {
    if (v.is_number()) {
      out = v.template get<double>();
      return true;
    }

    if (v.is_string()) {
      try {
        out = std::stod(v.template get<std::string>());
        return true;
      } catch (...) {
        return false;
      }
    }

    return false;
  }

  bool point_in_polygon(lv_coord_t x, lv_coord_t y, const std::vector<lv_point_t> &poly) {
    if (poly.size() < 3) {
      return false;
    }

    bool inside = false;
    size_t j = poly.size() - 1;
    for (size_t i = 0; i < poly.size(); j = i++) {
      const lv_point_t &pi = poly[i];
      const lv_point_t &pj = poly[j];
      if ((pi.y > y) != (pj.y > y)) {
        double x_intersect = static_cast<double>(pj.x - pi.x)
          * static_cast<double>(y - pi.y)
          / static_cast<double>(pj.y - pi.y)
          + static_cast<double>(pi.x);
        if (static_cast<double>(x) < x_intersect) {
          inside = !inside;
        }
      }
    }

    return inside;
  }
} // namespace

ExcludeObjectPanel::ExcludeObjectPanel(KWebSocketClient &websocket_client, std::mutex &l)
  : NotifyConsumer(l)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , canvas(lv_canvas_create(panel_cont))
  , canvas_dim(calc_canvas_dim())
  , canvas_buf(static_cast<lv_color_t *>(malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(canvas_dim, canvas_dim))))
  , info_cont(lv_obj_create(panel_cont))
  , status_label(lv_label_create(info_cont))
  , back_btn(info_cont, &back, "Back", &ExcludeObjectPanel::_handle_callback, this)
{
  lv_obj_move_background(panel_cont);
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(panel_cont, 6, 0);

  lv_canvas_set_buffer(canvas, canvas_buf, canvas_dim, canvas_dim, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(canvas, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_set_style_border_width(canvas, 2, 0);
  lv_obj_set_style_border_color(canvas, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
  lv_obj_set_style_radius(canvas, 4, 0);
  lv_canvas_fill_bg(canvas, lv_color_make(30, 30, 30), LV_OPA_COVER);

  lv_obj_add_flag(panel_cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(panel_cont, &ExcludeObjectPanel::_handle_canvas_click, LV_EVENT_RELEASED, this);

  lv_obj_set_style_border_width(info_cont, 0, 0);
  lv_obj_set_style_bg_opa(info_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(info_cont, 4, 0);
  lv_obj_set_style_pad_top(info_cont, 24, 0);
  lv_obj_set_style_pad_bottom(info_cont, 48, 0);
  lv_obj_clear_flag(info_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(info_cont, LV_PCT(48), LV_PCT(100));
  lv_obj_align(info_cont, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_flex_flow(info_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(info_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_label_set_recolor(status_label, true);
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(status_label, LV_PCT(100));
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(status_label, 0, 0);

  lv_obj_set_parent(back_btn.get_container(), panel_cont);
  lv_obj_set_width(back_btn.get_container(), LV_PCT(18));
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, -8, -8);

  ws.register_notify_update(this);
}

ExcludeObjectPanel::~ExcludeObjectPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }

  if (canvas_buf != NULL) {
    free(canvas_buf);
    canvas_buf = NULL;
  }

  ws.unregister_notify_update(this);
}

void ExcludeObjectPanel::foreground() {
  is_foreground = true;
  load_bed_bounds();
  redraw();
  lv_obj_move_foreground(panel_cont);
}

void ExcludeObjectPanel::consume(json &j) {
  auto pstate = j["/params/0/print_stats/state"_json_pointer];
  if (!pstate.is_null()) {
    std::string print_status = pstate.template get<std::string>();
    if (print_status != "printing" && print_status != "paused") {
      std::lock_guard<std::mutex> lock(lv_lock);
      is_foreground = false;
      pending_name.clear();
      confirm_mbox = nullptr;
      lv_obj_move_background(panel_cont);
      return;
    }
  }

  auto eo = j["/params/0/exclude_object"_json_pointer];
  if (eo.is_null() || !is_foreground) {
    return;
  }

  std::lock_guard<std::mutex> lock(lv_lock);
  redraw();
}

void ExcludeObjectPanel::load_bed_bounds() {
  auto s = State::get_instance();
  auto stepper_x_max = s->get_data("/printer_state/configfile/config/stepper_x/position_max"_json_pointer);
  auto stepper_y_max = s->get_data("/printer_state/configfile/config/stepper_y/position_max"_json_pointer);
  double max_x = 0.0;
  double max_y = 0.0;
  if (parse_scalar(stepper_x_max, max_x) && parse_scalar(stepper_y_max, max_y)) {
    bed_min_x = 0.0;
    bed_min_y = 0.0;
    bed_max_x = max_x;
    bed_max_y = max_y;
  }

  if (bed_max_x - bed_min_x < 1.0) {
    bed_min_x = 0.0;
    bed_max_x = 220.0;
  }

  if (bed_max_y - bed_min_y < 1.0) {
    bed_min_y = 0.0;
    bed_max_y = 220.0;
  }
}

lv_point_t ExcludeObjectPanel::to_px(double mx, double my) {
  double bw = bed_max_x - bed_min_x;
  double bh = bed_max_y - bed_min_y;
  double avail = canvas_dim - 2 * MARGIN;
  double scale = avail / std::max(bw, bh);
  double ox = MARGIN + (avail - bw * scale) / 2.0;
  double oy = MARGIN + (avail - bh * scale) / 2.0;

  lv_point_t p;
  p.x = static_cast<lv_coord_t>(std::lround(ox + (mx - bed_min_x) * scale));
  p.y = static_cast<lv_coord_t>(std::lround(canvas_dim - oy - (my - bed_min_y) * scale));
  return p;
}

void ExcludeObjectPanel::redraw() {
  obj_boxes.clear();
  lv_canvas_fill_bg(canvas, lv_color_make(30, 30, 30), LV_OPA_COVER);

  lv_point_t bl = to_px(bed_min_x, bed_min_y);
  lv_point_t tr = to_px(bed_max_x, bed_max_y);

  lv_draw_rect_dsc_t bed_dsc;
  lv_draw_rect_dsc_init(&bed_dsc);
  bed_dsc.bg_opa = LV_OPA_TRANSP;
  bed_dsc.border_color = lv_palette_darken(LV_PALETTE_GREY, 2);
  bed_dsc.border_width = 2;
  bed_dsc.border_opa = LV_OPA_COVER;
  lv_canvas_draw_rect(canvas, tr.x, tr.y, bl.x - tr.x, bl.y - tr.y, &bed_dsc);

  auto s = State::get_instance();
  auto objects = s->get_data("/printer_state/exclude_object/objects"_json_pointer);
  auto excluded = s->get_data("/printer_state/exclude_object/excluded_objects"_json_pointer);
  auto current = s->get_data("/printer_state/exclude_object/current_object"_json_pointer);
  std::string current_name = current.is_string() ? current.template get<std::string>() : "";

  auto is_excluded = [&excluded](const std::string &name) {
    if (!excluded.is_array()) {
      return false;
    }

    for (auto &e : excluded) {
      if (e.is_string() && e.template get<std::string>() == name) {
        return true;
      }

      if (e.is_object() && e.contains("name") && e["name"] == name) {
        return true;
      }
    }

    return false;
  };

  if (!objects.is_array() || objects.empty()) {
    lv_label_set_text(status_label, "No excludable objects.\n\nThe gcode must be sliced\nwith object labels.");
    return;
  }

  int idx = 0;
  int n_excluded = 0;
  for (auto &obj : objects) {
    if (!obj.contains("name")) {
      continue;
    }

    std::string name = obj["name"].template get<std::string>();
    bool excl = is_excluded(name);
    bool cur = name == current_name;
    if (excl) {
      n_excluded++;
    }

    lv_color_t color = excl ? lv_palette_darken(LV_PALETTE_RED, 2)
                            : (cur ? lv_palette_main(LV_PALETTE_GREEN)
                                   : lv_palette_main(LV_PALETTE_BLUE));

    std::vector<lv_point_t> pts;
    if (obj.contains("polygon") && obj["polygon"].is_array() && !obj["polygon"].empty()) {
      for (auto &v : obj["polygon"]) {
        if (v.is_array() && v.size() >= 2) {
          double px = v[0].template get<double>();
          double py = v[1].template get<double>();
          pts.push_back(to_px(px, py));
        }
      }
    } else if (obj.contains("center") && obj["center"].is_array() && obj["center"].size() >= 2) {
      double cx = obj["center"][0].template get<double>();
      double cy = obj["center"][1].template get<double>();
      pts.push_back(to_px(cx - 5, cy - 5));
      pts.push_back(to_px(cx + 5, cy - 5));
      pts.push_back(to_px(cx + 5, cy + 5));
      pts.push_back(to_px(cx - 5, cy + 5));
    }

    if (pts.empty()) {
      continue;
    }

    lv_coord_t x0 = pts[0].x;
    lv_coord_t y0 = pts[0].y;
    lv_coord_t x1 = pts[0].x;
    lv_coord_t y1 = pts[0].y;
    for (auto &p : pts) {
      x0 = std::min(x0, p.x);
      y0 = std::min(y0, p.y);
      x1 = std::max(x1, p.x);
      y1 = std::max(y1, p.y);
    }
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = 3;
    line.opa = LV_OPA_COVER;
    for (size_t i = 0; i < pts.size(); i++) {
      lv_point_t seg[2] = {pts[i], pts[(i + 1) % pts.size()]};
      lv_canvas_draw_line(canvas, seg, 2, &line);
    }

    lv_coord_t cx = (x0 + x1) / 2;
    lv_coord_t cy = (y0 + y1) / 2;
    lv_coord_t diameter = 24;
    lv_coord_t radius = diameter / 2;
    obj_boxes.push_back({name, idx + 1, x0, y0, x1, y1, cx, cy, radius, excl, pts});

    lv_draw_rect_dsc_t marker;
    lv_draw_rect_dsc_init(&marker);
    marker.bg_color = color;
    marker.bg_opa = excl ? LV_OPA_30 : LV_OPA_70;
    marker.border_color = color;
    marker.border_width = 2;
    marker.border_opa = LV_OPA_COVER;
    marker.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(canvas, cx - radius, cy - radius, diameter, diameter, &marker);

    if (excl) {
      lv_point_t d1[2] = {{x0, y0}, {x1, y1}};
      lv_point_t d2[2] = {{x0, y1}, {x1, y0}};
      lv_canvas_draw_line(canvas, d1, 2, &line);
      lv_canvas_draw_line(canvas, d2, 2, &line);
    }

    lv_draw_label_dsc_t lbl;
    lv_draw_label_dsc_init(&lbl);
    lbl.color = lv_color_white();
    lbl.font = &lv_font_montserrat_14;
    lv_canvas_draw_text(canvas, cx - 6, cy - 8, 20, &lbl, std::to_string(idx + 1).c_str());
    idx++;
  }

  lv_label_set_text(status_label,
                    fmt::format("#4caf50 Printing now#\n"
                                "#2196f3 Tap to exclude#\n"
                                "#b71c1c Excluded#\n\n"
                                "{} object(s), {} excluded",
                                static_cast<int>(objects.size()), n_excluded).c_str());
}

void ExcludeObjectPanel::handle_canvas_click(lv_event_t *e) {
  (void)e;
  if (confirm_mbox != nullptr) {
    return;
  }

  lv_indev_t *indev = lv_indev_get_act();
  if (indev == NULL) {
    return;
  }

  lv_point_t point;
  lv_indev_get_point(indev, &point);

  lv_area_t coords;
  lv_obj_get_coords(canvas, &coords);
  lv_coord_t cx = point.x - coords.x1;
  lv_coord_t cy = point.y - coords.y1;

  const ObjBox *hit = nullptr;
  long best_area = 0;
  for (auto &b : obj_boxes) {
    if (b.excluded) {
      continue;
    }

    if (point_in_polygon(cx, cy, b.polygon)) {
      long area = static_cast<long>(std::max<lv_coord_t>(b.x1 - b.x0, 1))
        * static_cast<long>(std::max<lv_coord_t>(b.y1 - b.y0, 1));
      if (hit == nullptr || area < best_area) {
        hit = &b;
        best_area = area;
      }
    }
  }

  if (hit != nullptr) {
    confirm_exclude(*hit);
  }
}

void ExcludeObjectPanel::confirm_exclude(const ObjBox &obj) {
  pending_name = obj.name;

  auto cur = State::get_instance()->get_data("/printer_state/exclude_object/current_object"_json_pointer);
  bool printing_now = cur.is_string() && cur.template get<std::string>() == obj.name;
  std::string msg = printing_now
    ? fmt::format("Stop printing object {}?\nThis cannot be undone.", obj.number)
    : fmt::format("Exclude object {}?\nThis cannot be undone.", obj.number);

  static const char *btns[] = {"Cancel", "Exclude", ""};
  SimpleDialogOptions options{};
  options.buttons = btns;
  options.error = true;
  options.auto_close = true;
  options.multiline_message = true;
  options.highlighted_button_idx = 1;
  options.result_cb = handle_exclude_dialog_result;
  options.user_data = this;
  confirm_mbox = create_configurable_dialog(lv_scr_act(), "Exclude Object", msg.c_str(), options);
}

void ExcludeObjectPanel::do_exclude() {
  if (pending_name.empty()) {
    return;
  }

  LOG_INFO("excluding object {}", pending_name);
  ws.gcode_script(fmt::format("EXCLUDE_OBJECT NAME={}", pending_name));
  pending_name.clear();
}

void ExcludeObjectPanel::handle_dialog_result(uint32_t button_idx) {
  confirm_mbox = nullptr;
  if (button_idx == 1) {
    do_exclude();
  } else {
    pending_name.clear();
  }
}

void ExcludeObjectPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }

  lv_obj_t *btn = lv_event_get_current_target(e);
  if (btn == back_btn.get_container()) {
    is_foreground = false;
    lv_obj_move_background(panel_cont);
  }
}
