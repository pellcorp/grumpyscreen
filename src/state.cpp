#include "state.h"
#include "config.h"
#include "utils.h"
#include "logger.h"
#include "lvgl/lvgl.h"

const uint32_t GUPPY_COLOR_SIZE = 19;
const lv_palette_t GUPPY_COLORS[GUPPY_COLOR_SIZE] = {
  LV_PALETTE_RED,
  LV_PALETTE_PINK,
  LV_PALETTE_PURPLE,
  LV_PALETTE_DEEP_PURPLE,
  LV_PALETTE_INDIGO,
  LV_PALETTE_BLUE,
  LV_PALETTE_LIGHT_BLUE,
  LV_PALETTE_CYAN,
  LV_PALETTE_TEAL,
  LV_PALETTE_GREEN,
  LV_PALETTE_LIGHT_GREEN,
  LV_PALETTE_LIME,
  LV_PALETTE_YELLOW,
  LV_PALETTE_AMBER,
  LV_PALETTE_ORANGE,
  LV_PALETTE_DEEP_ORANGE,
  LV_PALETTE_BROWN,
  LV_PALETTE_BLUE_GREY,
  LV_PALETTE_GREY
};

std::mutex State::lock;
State *State::instance{NULL};

State::State(std::mutex &state_lock)
  : NotifyConsumer(state_lock)
{
}

State *State::get_instance() {
  if (instance == NULL) {
    instance = new State(State::lock);
  }
  return instance;
}

void State::reset() {
  std::lock_guard<std::mutex> guard(lock);
  data.clear();
}

void State::set_data(const std::string &key, json &j, const std::string &json_path) {
  std::lock_guard<std::mutex> guard(lock);
  auto patch = j[json::json_pointer(json_path)];
  if (!patch.is_null()) {
    data[key].merge_patch(patch);
  }
}

json &State::get_data() {
  std::lock_guard<std::mutex> guard(lock);
  return data;
}

json &State::get_data(const json::json_pointer& ptr) {
  std::lock_guard<std::mutex> guard(lock);
  return data[ptr];
}

void State::consume(json &j) {
  if (j.contains("params") && !j["params"].empty()) {
    set_data("printer_state", j, "/params/0");
  }
}

std::vector<std::string> State::get_extruders() {
  std::lock_guard<std::mutex> guard(lock);
  auto &objects = data["/printer_objs/objects"_json_pointer];
  std::vector<std::string> extruders;
  if (!objects.is_null()) {
    for (auto &o : objects) {
      const std::string &obj_name = o.template get<std::string>();
      if (obj_name.rfind("extruder", 0) == 0 && obj_name.rfind("extruder_stepper", 0) != 0) {
	      extruders.push_back(obj_name);
      }
    }
  }

  return extruders;
}
  
std::vector<std::string> State::get_heaters() {
  std::lock_guard<std::mutex> guard(lock);
  auto &objects = data["/printer_objs/objects"_json_pointer];
  std::vector<std::string> heaters;
  if (!objects.is_null()) {
    for (auto &o : objects) {
      const std::string &obj_name = o.template get<std::string>();
      if (obj_name == "heater_bed"|| obj_name.rfind("heater_generic ", 0) == 0) {
	      heaters.push_back(obj_name);
      }
    }
  }

  return heaters;
}

std::vector<std::string> State::get_sensors() {
  std::lock_guard<std::mutex> guard(lock);
  auto &objects = data["/printer_objs/objects"_json_pointer];
  std::vector<std::string> sensors;
  if (!objects.is_null()) {
    for (auto &o : objects) {
      const std::string &obj_name = o.template get<std::string>();
      if (obj_name.rfind("temperature_sensor ", 0) == 0 || obj_name.rfind("temperature_fan ", 0) == 0) {
	      sensors.push_back(obj_name);
      }
    }
  }

  return sensors;
}

std::vector<std::string> State::get_fans() {
  std::lock_guard<std::mutex> guard(lock);
  auto &objects = data["/printer_objs/objects"_json_pointer];
  std::vector<std::string> fans;
  if (!objects.is_null()) {
    for (auto &o : objects) {
      const std::string &obj_name = o.template get<std::string>();
      if (obj_name == "fan"
          || obj_name.rfind("heater_fan ", 0) == 0
          || obj_name.rfind("fan_generic ", 0) == 0
          || obj_name.rfind("controller_fan ", 0) == 0) {
	      fans.push_back(obj_name);
      }
    }
  }

  return fans;
}

std::vector<std::string> State::get_leds() {
  std::lock_guard<std::mutex> guard(lock);
  auto &objects = data["/printer_objs/objects"_json_pointer];
  std::vector<std::string> leds;
  if (!objects.is_null()) {
    for (auto &o : objects) {
      const std::string &obj_name = o.template get<std::string>();
      if (obj_name.rfind("led ", 0) == 0) {
	      leds.push_back(obj_name);
      }
    }
  }

  return leds;
}

std::vector<std::string> State::get_output_pins() {
  std::lock_guard<std::mutex> guard(lock);
  auto &objects = data["/printer_objs/objects"_json_pointer];
  std::vector<std::string> output_pins;
  if (!objects.is_null()) {
    for (auto &o : objects) {
      const std::string &obj_name = o.template get<std::string>();
      if (obj_name.rfind("output_pin ", 0) == 0) {
	      output_pins.push_back(obj_name);
      }
    }
  }

  return output_pins;
}

json State::get_display_sensors() {
  Config *conf = Config::get_instance();
  const auto& user_sensors = conf->get_objects("/monitored_sensor");
  json sensors_by_id;
  if (!user_sensors.empty()) {
    for (auto &s : user_sensors) {
      sensors_by_id[s["id"].template get<std::string>()] = s;
    }
  }

  json display_sensors;
  auto extruders = get_extruders();
  for (auto &e : extruders) {
    if (sensors_by_id.contains(e)) {
      LOG_DEBUG("found user configured extruder {}", e);
      display_sensors[e] = sensors_by_id[e];
    }
  }

  auto heaters = get_heaters();
  for (auto &e : heaters) {
    if (sensors_by_id.contains(e)) {
      LOG_DEBUG("found user configured heater {}", e);
      display_sensors[e] = sensors_by_id[e];
    }
  }

  auto sensors = get_sensors();
  for (auto &e : sensors) {
    if (sensors_by_id.contains(e)) {
      LOG_DEBUG("found user configured sensor {}", e);
      display_sensors[e] = sensors_by_id[e];
    }
  }
  return display_sensors;
}

json State::get_display_fans() {
  Config *conf = Config::get_instance();
  const auto& user_fans = conf->get_objects("/fan");
  json fans_by_id;
  if (!user_fans.empty()) {
    for (auto &s : user_fans) {
      fans_by_id[s["id"].template get<std::string>()] = s;
    }
  }

  json display_fans;
  auto fans = get_fans();
  for (auto &e : fans) {
    if (fans_by_id.contains(e)) {
      LOG_DEBUG("found user configured fan {}", e);
      display_fans[e] = fans_by_id[e];
    }
  }

  auto output_pins = get_output_pins();
  for (auto &e : output_pins) {
    if (fans_by_id.contains(e)) {
      LOG_DEBUG("found user configured output_pin fan {}", e);
      display_fans[e] = fans_by_id[e];
    }
  }
  return display_fans;
}

json State::get_display_leds() {
  Config *conf = Config::get_instance();
  const auto& user_leds = conf->get_objects("/led");

  std::vector<std::string> user_led_ids;
  if (!user_leds.empty()) {
    for (auto &s : user_leds) {
      user_led_ids.push_back(s["id"].template get<std::string>());
    }
  }

  std::vector<std::string> system_led_ids;
  auto leds = get_leds();
  for (auto &e : leds) {
    if (std::find(user_led_ids.begin(), user_led_ids.end(), e) != user_led_ids.end()) {
      LOG_DEBUG("found user configured led {}", e);
      system_led_ids.push_back(e);
    }
  }

  auto output_pins = get_output_pins();
  for (auto &e : output_pins) {
    if (std::find(user_led_ids.begin(), user_led_ids.end(), e) != user_led_ids.end()) {
      LOG_DEBUG("found user configured output_pin leds {}", e);
      system_led_ids.push_back(e);
    }
  }

  json display_leds;
  for (auto &s : user_leds) {
    auto id = s["id"].template get<std::string>();
    if (std::find(system_led_ids.begin(), system_led_ids.end(), id) != system_led_ids.end()) {
      display_leds.push_back(s);
    }
  }
  return display_leds;
}
