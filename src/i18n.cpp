#include "i18n.h"
#include "hv/json.hpp"

#include <algorithm>
#include <cstring>
#include <deque>
#include <experimental/filesystem>
#include <fstream>
#include <unordered_map>

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace fs = std::experimental::filesystem;
using json = nlohmann::json;

namespace {

using Table = std::unordered_map<std::string, std::string>;

constexpr const char *NAME_KEY = "_language";

Table table;
std::string active = "en";

struct ButtonMap {
  std::vector<std::string> english;
  std::vector<const char *> shown;
};
// a deque so a map keeps its address when another is added
std::deque<ButtonMap> button_maps;

void refresh_map(ButtonMap &m) {
  for (size_t i = 0; i < m.english.size(); i++) {
    m.shown[i] = I18n::tr(m.english[i].c_str());
  }
}

bool read_json(const std::string &path, json &out) {
  std::ifstream in(path);
  if (!in) return false;
  out = json::parse(in, nullptr, false);
  return out.is_object();
}

}  // namespace

namespace I18n {

const char *tr(const char *text) {
  if (text == nullptr || table.empty()) return text;
  const auto found = table.find(text);
  return found == table.end() ? text : found->second.c_str();
}

const char **map(const char *const *english) {
  std::vector<std::string> keys;
  for (size_t i = 0; english[i][0] != '\0'; i++) keys.emplace_back(english[i]);

  for (ButtonMap &m : button_maps) {
    if (m.english == keys) return m.shown.data();
  }
  ButtonMap &m = button_maps.emplace_back();
  m.english = std::move(keys);
  m.shown.assign(m.english.size() + 1, "");
  refresh_map(m);
  return m.shown.data();
}

std::vector<Language> available(const std::string &dir) {
  std::vector<Language> languages;
  std::error_code ec;
  for (const auto &entry : fs::directory_iterator(dir, ec)) {
    if (entry.path().extension() != ".json") continue;
    const std::string code = entry.path().stem().string();
    json file;
    if (code == "en" || !read_json(entry.path().string(), file)) continue;
    const auto name = file.find(NAME_KEY);
    if (name != file.end() && name->is_string()) {
      languages.push_back({code, name->get<std::string>()});
    }
  }
  std::sort(languages.begin(), languages.end(),
            [](const Language &a, const Language &b) { return a.name < b.name; });
  languages.insert(languages.begin(), {"en", "English"});
  return languages;
}

const std::string &current() {
  return active;
}

size_t loaded_strings() {
  return table.size();
}

bool load(const std::string &dir, const std::string &code) {
  Table next;
  {
    json file;
    if (!read_json((fs::path(dir) / (code + ".json")).string(), file)) {
      // English is the text in the code, so it works without its file
      if (code != "en") return false;
      file = json::object();
    }
    for (auto it = file.begin(); it != file.end(); ++it) {
      if (it.key() != NAME_KEY && it->is_string()) next.emplace(it.key(), it->get<std::string>());
    }
  }  // the parsed file is gone before the old table goes

  table.swap(next);
  active = code;
  for (ButtonMap &m : button_maps) refresh_map(m);

  // releases the previous language; nothing points into it any more
  Table().swap(next);
#ifdef __GLIBC__
  malloc_trim(0);
#endif
  return true;
}

std::vector<std::pair<std::string, std::string>> reverse_table() {
  std::vector<std::pair<std::string, std::string>> reverse;
  reverse.reserve(table.size());
  for (const auto &entry : table) reverse.emplace_back(entry.second, entry.first);
  return reverse;
}

std::string default_dir() {
  return (fs::canonical("/proc/self/exe").parent_path() / "languages").string();
}

}  // namespace I18n
