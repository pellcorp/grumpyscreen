// test_config.cpp
#include <cassert>
#include <fstream>
#include <string>
#include <vector>
#include "config.h"

static std::string write_tmp_ini(const std::string& path, const std::string& text) {
    std::ofstream out(path);
    out << text;
    return path;
}

int main() {
    const std::string ini = R"INI(
# simple sections
[logging]
level: info

[moonraker]
host: 127.0.0.1
port: 7125

[display]
brightness: 90
rotate: 3
sleep_sec: 600

[ui]
extruder_temp_presets: 190, 220, 245
extruder_temp_default: 245
extruder_length_presets: 5, 25
extruder_length_default: 25
extruder_speed_presets: 2, 8, 16
extruder_speed_default: 8

[fan "fan"]
display_name: Toolhead

# repeated objects
[led "output_pin LED"]
display_name: LED
pwm: true

[led "output_pin red_pin"]
display_name: Red
pwm: false

[fan "fan_generic chamber"]
display_name: Chamber
)INI";

    const std::string override_ini = R"INI(
[moonraker]
host: moonraker.local
missing_key: ignored

[ui]
extruder_temp_default: 250
new_setting: ignored

[fan "fan"]
display_name: Toolhead Override
new_field: ignored

[fan "fan_generic nevermore"]
display_name: Nevermore

[led "output_pin white_pin"]
display_name: White
pwm: false

[monitored_sensor "temperature_sensor enclosure"]
display_name: Enclosure
color: green
controllable: true

[missing_section]
value: ignored
)INI";

    auto path = write_tmp_ini("build/test_config.ini", ini);
    auto override_path = write_tmp_ini("build/test_config_override.ini", override_ini);

    Config* conf = Config::get_instance();
    assert(conf->load(path) && "load should succeed");
    assert(conf->load_override(override_path) && "override load should succeed");

    // scalars
    assert(conf->get<std::string>("/logging/level") == "info");
    assert(conf->get<std::string>("/moonraker/host") == "moonraker.local");
    assert(conf->get<int32_t>("/moonraker/port") == 7125);
    assert(conf->get<int32_t>("/display/brightness") == 90);
    assert(conf->get<int32_t>("/display/rotate") == 3);
    assert(conf->get<int32_t>("/display/sleep_sec") == 600);
    assert(conf->get<std::string>("/ui/extruder_temp_presets") == "190, 220, 245");
    assert(conf->get<int32_t>("/ui/extruder_temp_default") == 250);
    assert(conf->get<std::string>("/ui/extruder_length_presets") == "5, 25");
    assert(conf->get<int32_t>("/ui/extruder_length_default") == 25);
    assert(conf->get<std::string>("/ui/extruder_speed_presets") == "2, 8, 16");
    assert(conf->get<int32_t>("/ui/extruder_speed_default") == 8);
    assert(conf->get<std::string>("/missing/key", "default") == "default");
    assert(conf->get<std::string>("/missing/key") == ""); // empty by default
    assert(conf->get<std::string>("/moonraker/missing_key", "default") == "default");
    assert(conf->get<std::string>("/ui/new_setting", "default") == "default");

    // objects
    auto leds = conf->get_objects("/led");
    assert(leds.size() == 3);

    // map by id for easy checks
    nlohmann::json by_id = nlohmann::json::object();
    for (const auto& o : leds) {
        assert(o.is_object());
        const auto it = o.find("id");
        assert(it != o.end() && it->is_string());
        by_id[it->get<std::string>()] = o;
    }

    assert(by_id.contains("output_pin LED"));
    assert(by_id["output_pin LED"]["display_name"] == "LED");
    assert(by_id["output_pin LED"]["pwm"] == true);

    assert(by_id.contains("output_pin red_pin"));
    assert(by_id["output_pin red_pin"]["display_name"] == "Red");
    assert(by_id["output_pin red_pin"]["pwm"] == false);

    auto fans = conf->get_objects("/fan");
    assert(fans.size() == 3);

		nlohmann::json fan_by_id = nlohmann::json::object();
		for (const auto& o : fans) {
				assert(o.is_object());
				const auto it = o.find("id");
				assert(it != o.end() && it->is_string());
				fan_by_id[it->get<std::string>()] = o;
		}

    assert(fan_by_id.contains("fan"));
    assert(fan_by_id["fan"]["display_name"] == "Toolhead Override");
    assert(!fan_by_id["fan"].contains("new_field"));
    assert(fan_by_id.contains("fan_generic nevermore"));

    nlohmann::json led_by_id = nlohmann::json::object();
    for (const auto& o : leds) {
        const auto it = o.find("id");
        assert(it != o.end() && it->is_string());
        led_by_id[it->get<std::string>()] = o;
    }
    assert(led_by_id.contains("output_pin white_pin"));

    auto sensors = conf->get_objects("/monitored_sensor");
    nlohmann::json sensor_by_id = nlohmann::json::object();
    for (const auto& o : sensors) {
        const auto it = o.find("id");
        assert(it != o.end() && it->is_string());
        sensor_by_id[it->get<std::string>()] = o;
    }
    assert(sensor_by_id.contains("temperature_sensor enclosure"));

    return 0;
}
