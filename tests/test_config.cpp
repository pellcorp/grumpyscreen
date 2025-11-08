// test_config.cpp
#include <cassert>
#include <fstream>
#include <string>
#include <vector>
#include "config.h"  // your class with get<T>(), get_objects(), load()

static std::string write_tmp_ini(const std::string& text) {
    const std::string path = "build/test_config.ini";
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

# repeated objects
[leds "output_pin LED"]
display_name: LED
pwm: true

[leds "output_pin red_pin"]
display_name: Red
pwm: false
)INI";

    auto path = write_tmp_ini(ini);

    Config* conf = Config::get_instance();
    assert(conf->load(path) && "load should succeed");

    // scalars
    assert(conf->get<std::string>("/logging/level") == "info");
    assert(conf->get<std::string>("/moonraker/host") == "127.0.0.1");
    assert(conf->get<int32_t>("/moonraker/port") == 7125);
    assert(conf->get<int32_t>("/display/brightness") == 90);
    assert(conf->get<int32_t>("/display/rotate") == 3);
    assert(conf->get<int32_t>("/display/sleep_sec") == 600);
    assert(conf->get<std::string>("/missing/key", "default") == "default");
    assert(conf->get<std::string>("/missing/key") == ""); // empty by default

    // objects
    auto leds = conf->get_objects("/leds");
    assert(leds.size() == 2);

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

    // set() round-trip in memory
    conf->set<std::string>("/logging/level", "debug");
    assert(conf->get<std::string>("/logging/level") == "debug");

		conf->set<std::string>("/logging/level", "debug");
		assert(conf->save() && "save should succeed");

		// reload fresh instance
		Config* conf2 = Config::get_instance();
		assert(conf2->load(path) && "reload should succeed");
		assert(conf2->get<std::string>("/logging/level") == "debug");

		// ensure arrays still intact after save
		auto leds2 = conf2->get_objects("/leds");
		assert(leds2.size() == 2);
		assert(leds2[0].contains("display_name"));

    return 0;
}
