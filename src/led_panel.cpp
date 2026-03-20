#include "led_panel.h"
#include "state.h"
#include "utils.h"
#include "logger.h"

namespace {
bool get_led_pwm(const json &led) {
  auto pwm_it = led.find("pwm");
  if (pwm_it != led.end() && pwm_it->is_boolean()) {
    return pwm_it->template get<bool>();
  }
  return true;
}

std::string get_led_display_name(const json &led, const std::string &fallback) {
  auto display_name_it = led.find("display_name");
  if (display_name_it != led.end() && display_name_it->is_string()) {
    return display_name_it->template get<std::string>();
  }
  return fallback;
}
} // namespace

LV_IMG_DECLARE(cancel);
LV_IMG_DECLARE(light_img);
LV_IMG_DECLARE(back);

LedPanel::LedPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , ledpanel_cont(lv_obj_create(lv_scr_act()))
  , leds_cont(lv_obj_create(ledpanel_cont))
  , back_btn(ledpanel_cont, &back, "Back", &LedPanel::_handle_callback, this)
{
    lv_obj_set_style_pad_all(ledpanel_cont, 0, 0);

    lv_obj_clear_flag(ledpanel_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ledpanel_cont, lv_pct(100), lv_pct(100));

    lv_obj_center(leds_cont);
    lv_obj_set_size(leds_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(leds_cont, LV_FLEX_FLOW_COLUMN);

    lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);
#ifdef GUPPY_SMALL_SCREEN
    lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 20, -10);
#else
    lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 30, -10);
#endif
    ws.register_notify_update(this);
}

LedPanel::~LedPanel() {
  if (ledpanel_cont != NULL) {
    lv_obj_del(ledpanel_cont);
    ledpanel_cont = NULL;
  }

  leds.clear();

  ws.unregister_notify_update(this);
}

void LedPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  for (auto &l : leds) {
    // hack for output_pin leds
    auto value = j[json::json_pointer(fmt::format("/params/0/{}/value", l.first))];
    if (!value.is_null()) {
      const double raw_value = value.template get<double>();
      int v = static_cast<int>(raw_value * 100);
      l.second->update_value(v);
      if (!single_led_id.empty() && l.first == single_led_id) {
        single_led_last_value = raw_value;
        single_led_last_value_valid = true;
      }
    }

    value = j[json::json_pointer(fmt::format("/params/0/{}/color_data", l.first))];
    if (!value.is_null() && value.size() > 0) {
      // color_data = [[r,b,g,w]]
      value =  value.at(0);
      if (value.size() == 4) {
        const double raw_value = value.at(3).template get<double>();
        int v = static_cast<int>(raw_value * 100);
        l.second->update_value(v);
        if (!single_led_id.empty() && l.first == single_led_id) {
          single_led_last_value = raw_value;
          single_led_last_value_valid = true;
        }
      }
    }
  }
}

void LedPanel::init(json &l) {
  std::lock_guard<std::mutex> lock(lv_lock);
  leds.clear();
  single_led_id.clear();
  single_led_is_output_pin = false;
  single_led_last_value = 0.0;
  single_led_last_value_valid = false;

  size_t created_led_count = 0;
  std::string created_led_id;
  bool created_led_is_output_pin = false;
  bool created_led_pwm = true;

  for (auto &led : l) {
    auto id_it = led.find("id");
    if (id_it == led.end() || !id_it->is_string()) {
      LOG_DEBUG("skipping malformed LED config entry: {}", led.dump());
      continue;
    }

    std::string key = id_it->template get<std::string>();
    LOG_DEBUG("create led {}, {}", l.dump(), led.dump());
    std::string display_name = get_led_display_name(led, KUtils::get_obj_name(key));
    bool pwm = get_led_pwm(led);
    const bool is_output_pin = key.rfind("output_pin ", 0) == 0;

    lv_event_cb_t null_cb = NULL;
    lv_event_cb_t led_cb = &LedPanel::_handle_led_update;
    if (!is_output_pin) {
      // standard led
      led_cb = &LedPanel::_handle_led_update_generic;
    }

    if (pwm) {
      auto lptr = std::make_shared<SliderContainer>(leds_cont, display_name.c_str(),
                &cancel, "Off", led_cb, this,
                &light_img, "Max", led_cb, this,
                led_cb, this, "%");
      auto inserted = leds.insert({key, lptr}).second;
      if (inserted) {
        ++created_led_count;
        created_led_id = key;
        created_led_is_output_pin = is_output_pin;
        created_led_pwm = pwm;
      }
    } else {
      auto lptr = std::make_shared<SliderContainer>(leds_cont, display_name.c_str(),
                    &cancel, "Off", led_cb, this,
                    &light_img, "On", led_cb, this,
                    null_cb, this, "%");
      auto inserted = leds.insert({key, lptr}).second;
      if (inserted) {
        ++created_led_count;
        created_led_id = key;
        created_led_is_output_pin = is_output_pin;
        created_led_pwm = pwm;
      }
    }
  }

  if (created_led_count == 1) {
    if (!created_led_pwm) {
      single_led_id = created_led_id;
      single_led_is_output_pin = created_led_is_output_pin;
      LOG_DEBUG("single LED mode enabled for {} (pwm={})",
                single_led_id,
                created_led_pwm);
    }
  }

#ifdef GUPPY_SMALL_SCREEN
  if (leds.size() > 2) {
#else
  if (leds.size() > 3) {
#endif
    lv_obj_set_size(leds_cont, lv_pct(90), lv_pct(100));
    lv_obj_align(leds_cont, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(leds_cont, LV_OBJ_FLAG_SCROLLABLE);
  } else {
    lv_obj_clear_flag(leds_cont, LV_OBJ_FLAG_SCROLLABLE);    
  }

  lv_obj_move_foreground(back_btn.get_container());
}

void LedPanel::activate() {
  if (!single_led_id.empty()) {
    toggle_single_led();
  } else {
    foreground();
  }
}

void LedPanel::foreground() {
  for (auto &l : leds) {
    // hack for output_pin leds
    auto led_value = State::get_instance()->get_data(json::json_pointer(fmt::format("/printer_state/{}/value", l.first)));
    if (!led_value.is_null()) {
      const double raw_value = led_value.template get<double>();
      int v = static_cast<int>(raw_value * 100);
      l.second->update_value(v);
      if (!single_led_id.empty() && l.first == single_led_id) {
        single_led_last_value = raw_value;
        single_led_last_value_valid = true;
      }
    }
    
    led_value = State::get_instance()->get_data(json::json_pointer(fmt::format("/printer_state/{}/color_data", l.first)));
    if (!led_value.is_null() && led_value.size() > 0) {
      // color_data = [[r,b,g,w]]
      led_value = led_value.at(0);
      if (led_value.size() == 4) {
        const double raw_value = led_value.at(3).template get<double>();
        int v = static_cast<int>(raw_value * 100);
        l.second->update_value(v);
        if (!single_led_id.empty() && l.first == single_led_id) {
          single_led_last_value = raw_value;
          single_led_last_value_valid = true;
        }
      }
    }
  }

  lv_obj_move_foreground(ledpanel_cont);
}

bool LedPanel::is_main_button_highlighted() {
  if (single_led_id.empty()) {
    return false;
  }

  const double current_value = single_led_last_value_valid
                                 ? single_led_last_value
                                 : get_led_value(single_led_id);
  return current_value > 0.0;
}

bool LedPanel::has_single_led_toggle_mode() {
  return !single_led_id.empty();
}

double LedPanel::get_led_value(const std::string &led_id) {
  auto value = State::get_instance()->get_data(
    json::json_pointer(fmt::format("/printer_state/{}/value", led_id)));
  if (!value.is_null() && value.is_number()) {
    return value.template get<double>();
  }

  value = State::get_instance()->get_data(
    json::json_pointer(fmt::format("/printer_state/{}/color_data", led_id)));
  if (!value.is_null() && value.is_array() && !value.empty()) {
    value = value.at(0);
    if (value.is_array() && value.size() >= 4 && value.at(3).is_number()) {
      return value.at(3).template get<double>();
    }
  }

  return 0.0;
}

void LedPanel::toggle_single_led() {
  const auto current_value = get_led_value(single_led_id);
  const bool turn_on = current_value <= 0.0;
  const double target_value = turn_on ? 1.0 : 0.0;
  const std::string led_name = KUtils::get_obj_name(single_led_id);

  if (single_led_is_output_pin) {
    ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE={}", led_name, target_value));
  } else {
    ws.gcode_script(fmt::format("SET_LED LED={} WHITE={}", led_name, target_value));
  }
  single_led_last_value = target_value;
  single_led_last_value_valid = true;
}

void LedPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == back_btn.get_container()) {
    lv_obj_move_background(ledpanel_cont);
  }
  else {
    LOG_DEBUG("Unknown action button pressed");
  }
}

void LedPanel::handle_led_update(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = (double)lv_slider_get_value(obj) / 100.0;

    LOG_DEBUG("updating led brightness to {}", pct);

    for (auto &l : leds) {
      if (obj == l.second->get_slider()) {
	      std::string led_name = KUtils::get_obj_name(l.first);
      	LOG_DEBUG("update led {}", led_name);
      	// TODO - I think this double fmt:format is intentional
        ws.gcode_script(fmt::format(fmt::format("SET_PIN PIN={} VALUE={}", led_name, pct)));
        break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    obj = lv_event_get_current_target(event);

    for (auto &l : leds) {
      if (obj == l.second->get_off()) {
	      std::string led_name = KUtils::get_obj_name(l.first);
      	LOG_DEBUG("turning off led {}", led_name);
        ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE=0", led_name));
        l.second->update_value(0);
        break;
      } else if (obj == l.second->get_max()) {
        std::string led_name = KUtils::get_obj_name(l.first);
        LOG_DEBUG("turning led to max {}", led_name);
        ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE=1", led_name));
        l.second->update_value(100);
        break;
      }
    }
  }
}

void LedPanel::handle_led_update_generic(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = (double)lv_slider_get_value(obj) / 100.0;

    LOG_DEBUG("updating led brightness to {}", pct);

    for (auto &l : leds) {
      if (obj == l.second->get_slider()) {
	      std::string led_name = KUtils::get_obj_name(l.first);
      	LOG_DEBUG("update led {}", led_name);
      	// TODO - I think this double fmt:format is intentional
        ws.gcode_script(fmt::format(fmt::format("SET_LED LED={} WHITE={}", led_name, pct)));
        break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    obj = lv_event_get_current_target(event);

    for (auto &l : leds) {
      if (obj == l.second->get_off()) {
	      std::string led_name = KUtils::get_obj_name(l.first);
      	LOG_DEBUG("turning off led {}", led_name);
        ws.gcode_script(fmt::format("SET_LED LED={} WHITE=0", led_name));
        l.second->update_value(0);
        break;
      } else if (obj == l.second->get_max()) {
        std::string led_name = KUtils::get_obj_name(l.first);
        LOG_DEBUG("turning led to max {}", led_name);
        ws.gcode_script(fmt::format("SET_LED LED={} WHITE=1", led_name));
        l.second->update_value(100);
        break;
      }
    }
  }
}
