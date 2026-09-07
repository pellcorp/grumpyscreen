#include "sensor_container.h"
#include "logger.h"
#include "theme.h"
#include "utils.h"
#include <string>

using namespace Theme;

SensorContainer::SensorContainer(KWebSocketClient &c,
				 lv_obj_t *parent,
				 const void *img,
				 const char *text,
				 lv_color_t color,
				 bool can_edit,
				 bool show_target,
				 Numpad &np,
				 std::string name,
				 lv_obj_t *chart_chart,
				 lv_chart_series_t *chart_series)
  : ws(c)
  , sensor_cont(lv_obj_create(parent))
  , accent(lv_obj_create(sensor_cont))
  , sensor_img(lv_img_create(sensor_cont))
  , sensor_label(lv_label_create(sensor_cont))
  , value_label(lv_label_create(sensor_cont))
  , value(0)
  , divider_label(lv_label_create(sensor_cont))
  , target_label(lv_label_create(sensor_cont))
  , target(-1)
  , numpad(np)
  , id(name)
  , chart(chart_chart)
  , series(chart_series)
  , last_updated_ts(std::time(nullptr))
{
    // A row card laid out with flex instead of seven hand-tuned offsets from
    // the right edge: the name takes the slack, so the row fills whatever
    // width it is given at any resolution and nothing collides.
    lv_obj_clear_flag(sensor_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(sensor_cont, &styles().row_card, 0);
    lv_obj_set_width(sensor_cont, LV_PCT(100));
    // one line tall unless the panel says otherwise (the home column stretches
    // its rows to share the space above the chart)
    lv_obj_set_height(sensor_cont, scale_r(30));
    lv_obj_set_flex_flow(sensor_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sensor_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sensor_cont, gap(), 0);
    // half the card gap top and bottom: a 30px row has no room for a full one
    const lv_coord_t pad_v = gap() / 2;
    lv_obj_set_style_pad_ver(sensor_cont, pad_v, 0);

    // The sensor's colour fills the card's left edge, top to bottom, just
    // inside the hairline. The bar floats out of the flex row, is placed at
    // the content's left minus the padding, and is stretched by the vertical
    // padding so it spans the whole card; the card clips it to its corners.
    // (The clip is a mask on every redraw of the row; a 30px row is cheap
    // enough, and the edge-to-edge bar is the look.)
    const lv_coord_t bar = gap() * 2 / 3;
    lv_obj_set_style_pad_left(sensor_cont, 2 * bar, 0);
    lv_obj_set_style_clip_corner(sensor_cont, true, 0);
    lv_obj_add_style(accent, &styles().row, 0);
    lv_obj_add_flag(accent, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(accent, bar, LV_PCT(100));
    lv_obj_set_pos(accent, -2 * bar, 0);
    lv_obj_set_style_transform_height(accent, 2 * pad_v, 0);
    lv_obj_set_style_bg_color(accent, color, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);

    // the icon follows the row's content height (icons are square bitmaps),
    // now and whenever the panel resizes the row
    lv_img_set_src(sensor_img, img);
    lv_obj_add_event_cb(sensor_cont, [](lv_event_t *e) {
      lv_obj_t *row = lv_event_get_target(e);
      const int h = lv_obj_get_content_height(row);
      fit_img(lv_obj_get_child(row, 1), h, h);  // child 0 is the accent bar
    }, LV_EVENT_SIZE_CHANGED, NULL);

    lv_label_set_text(sensor_label, text);
    lv_obj_set_flex_grow(sensor_label, 1);
    lv_label_set_long_mode(sensor_label, LV_LABEL_LONG_DOT);  // the values keep their room

    lv_label_set_text(value_label, "0");
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_label_set_text(divider_label, "/");

    if (show_target || can_edit) {
      lv_label_set_text(target_label, "0");
      lv_obj_set_style_text_align(target_label, LV_TEXT_ALIGN_CENTER, 0);
    } else {
      lv_obj_add_flag(target_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(divider_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (can_edit) {
      // the target is tappable, so it wears the button look; a minimum width
      // keeps "0" and "250" chips the same shape
      lv_obj_add_style(target_label, &styles().btn, 0);
      lv_obj_set_style_pad_hor(target_label, gap(), 0);
      lv_obj_set_style_pad_ver(target_label, gap() / 3, 0);
      lv_obj_set_style_min_width(target_label, scale_w(36), 0);

      LOG_DEBUG("sensor cb registered name {}", id);
      lv_obj_add_event_cb(sensor_cont, &SensorContainer::_handle_edit, LV_EVENT_CLICKED, this);
    } 
}

SensorContainer::~SensorContainer() {
  if (sensor_cont != NULL) {
    LOG_DEBUG("deleting sensor {}", id);
    lv_obj_del(sensor_cont);
    sensor_cont = NULL;
  }

  if (series != NULL && chart != NULL) {
    lv_chart_remove_series(chart, series);
    series = NULL;
  }
}

lv_obj_t *SensorContainer::get_sensor() {
  return sensor_cont;
}

void SensorContainer::update_target(int new_target) {
  if (new_target >= 0 && new_target != target) {
    target = new_target;
    lv_label_set_text(target_label, fmt::format("{}", new_target).c_str());
  }
}

void SensorContainer::update_value(int new_value) {
  if (value != new_value) {
    value = new_value;
    lv_label_set_text(value_label, fmt::format("{}", new_value).c_str());
  }
}

void SensorContainer::update_series(int v) {
  if (series != NULL && chart != NULL) {
    auto delta = std::time(nullptr) - last_updated_ts;
    if (delta > 1) {
      // The first reading fills the whole history. lv_chart's dense-points
      // mode seeds its running min/max from the point before the first real
      // one, and an unset point there drew every trace as a spike from the top.
      if (!series_seeded) {
        lv_chart_set_all_value(chart, series, v);
        series_seeded = true;
      }
      lv_chart_set_next_value(chart, series, v);
      last_updated_ts = std::time(nullptr);
    }
  }
}

void SensorContainer::handle_edit(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    LOG_TRACE("sensor callback this {}, {}, {}", id, fmt::ptr(this), fmt::ptr(&numpad));
    numpad.set_callback([this](double v) {
      std::string heater_name = KUtils::get_obj_name(id);
      if (id.find("temperature_fan") != std::string::npos) {
        ws.gcode_script(fmt::format("SET_TEMPERATURE_FAN_TARGET TEMPERATURE_FAN={} TARGET={}", heater_name, v));
      } else {
        ws.gcode_script(fmt::format("SET_HEATER_TEMPERATURE HEATER={} TARGET={}", heater_name, v));
      }
    });
    numpad.foreground_reset();
  }
}
