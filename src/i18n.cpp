#include "i18n.h"
#include "hv/json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>
using json = nlohmann::json;

static std::unordered_map<std::string, std::string> &tbl() {
  static std::unordered_map<std::string, std::string> t;
  return t;
}

void i18n_load(const std::string &path) {
  std::ifstream f(path);
  if (!f.good()) return;
  try {
    json j = json::parse(f);
    for (auto &it : j.items())
      if (it.value().is_string()) tbl()[it.key()] = it.value().get<std::string>();
  } catch (...) {}
}

const char *tr(const char *en) {
  auto &t = tbl();
  auto it = t.find(en);
  return it == t.end() ? en : it->second.c_str();
}

std::vector<std::string> i18n_list(const std::string &dir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (auto &e : std::filesystem::directory_iterator(dir, ec))
    if (e.path().extension() == ".json") out.push_back(e.path().stem().string());
  std::sort(out.begin(), out.end());
  return out;
}
