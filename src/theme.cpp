#include "theme.h"
#include "platform.h"
#include "logger.h"
#include <sys/stat.h>
#include <fstream>
#include <iomanip>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

ThemeConfig *ThemeConfig::instance{NULL};

ThemeConfig::ThemeConfig() {
}

ThemeConfig *ThemeConfig::get_instance() {
  if (instance == NULL) {
    instance = new ThemeConfig();
  }
  return instance;
}

void ThemeConfig::init(const std::string config_path) {
  LOG_INFO("Theme path is: {}", config_path);
  path = config_path;
  struct stat buffer;

  if (stat(config_path.c_str(), &buffer) == 0) {
    data = json::parse(std::fstream(config_path));
  } else {
    LOG_ERROR("Theme file not found: {}", config_path);
    data = {
      {"primary_color", "0x2196F3"}, //blue
      {"secondary_color", "0xF44336"} // red
    };
  }

  std::ofstream o(config_path);
  o << std::setw(2) << data << std::endl;
}

