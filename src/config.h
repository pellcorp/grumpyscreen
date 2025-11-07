#ifndef __K_CONFIG_H__
#define __K_CONFIG_H__

#include "hv/json.hpp"
#include "logger.h"

#include <string>

using json = nlohmann::json;

class Config {
 private:
  static Config *instance;
  std::string path;
  // this is dodgy af, I want a way to return an empty
  // object from get_json to avoid mutating the data object
  // with a new field, but I don't know if there is a better way
  json empty = json(nullptr);

 protected:
  json data;
  std::string default_printer;

 public:
  Config();
  Config(Config &o) = delete;
  void operator=(const Config &) = delete;
  void init(std::string config_path);

  template<typename T> T get(const std::string &json_ptr) {
    return data[json::json_pointer(json_ptr)].template get<T>();
  };

  template<typename T> T set(const std::string &json_ptr, T v) {
    return data[json::json_pointer(json_ptr)] = v;
  };

  json &get_json(const std::string &json_path);

  void save();
  std::string get_wifi_interface();
  std::string get_path();

  static Config *get_instance();

};

#endif // __K_CONFIG_H__
