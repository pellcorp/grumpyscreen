#include "led_panel.h"
#include "state.h"
#include "utils.h"
#include "logger.h"

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
    lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -10);

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
      int v = static_cast<int>(value.template get<double>() * 100);
      l.second->update_value(v);
    }

    value = j[json::json_pointer(fmt::format("/params/0/{}/color_data", l.first))];
    if (!value.is_null() && value.size() > 0) {
      // color_data = [[r,b,g,w]]
      value =  value.at(0);
      if (value.size() == 4) {
        int v = static_cast<int>(value.at(3).template get<double>() * 100);
        l.second->update_value(v);
      }
    }
  }
}

void LedPanel::init(json &l) {
  std::lock_guard<std::mutex> lock(lv_lock);
  leds.clear();
  for (auto &led : l) {
    std::string key = led["id"].template get<std::string>();
    LOG_DEBUG("create led {}, {}", l.dump(), led.dump());
    std::string display_name = led["display_name"].template get<std::string>();
    bool pwm = led["pwm"].is_null() ? true : led["pwm"].template get<bool>();

    lv_event_cb_t null_cb = NULL;
    lv_event_cb_t led_cb = &LedPanel::_handle_led_update;
    if (key.rfind("output_pin ", 0) != 0) {
      // standard led
      led_cb = &LedPanel::_handle_led_update_generic;
    }

    if (pwm) {
      auto lptr = std::make_shared<SliderContainer>(leds_cont, display_name.c_str(),
                &cancel, "Off", led_cb, this,
                &light_img, "Max", led_cb, this,
                led_cb, this, "%");
      leds.insert({key, lptr});
    } else {
      auto lptr = std::make_shared<SliderContainer>(leds_cont, display_name.c_str(),
                    &cancel, "Off", led_cb, this,
                    &light_img, "On", led_cb, this,
                    null_cb, this, "%");
      leds.insert({key, lptr});
    }
  }

  if (leds.size() > 4) {
    lv_obj_add_flag(leds_cont, LV_OBJ_FLAG_SCROLLABLE);
  } else {
    lv_obj_clear_flag(leds_cont, LV_OBJ_FLAG_SCROLLABLE);    
  }

  lv_obj_move_foreground(back_btn.get_container());
}

void LedPanel::foreground() {
  for (auto &l : leds) {
    // hack for output_pin leds
    auto led_value = State::get_instance()->get_data(json::json_pointer(fmt::format("/printer_state/{}/value", l.first)));
    if (!led_value.is_null()) {
      int v = static_cast<int>(led_value.template get<double>() * 100);
      l.second->update_value(v);
    }
    
    led_value = State::get_instance()->get_data(json::json_pointer(fmt::format("/printer_state/{}/color_data", l.first)));
    if (!led_value.is_null() && led_value.size() > 0) {
      // color_data = [[r,b,g,w]]
      led_value = led_value.at(0);
      if (led_value.size() == 4) {
        int v = static_cast<int>(led_value.at(3).template get<double>() * 100);
        l.second->update_value(v);
      }
    }
  }

  lv_obj_move_foreground(ledpanel_cont);
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
