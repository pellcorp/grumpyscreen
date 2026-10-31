#ifndef __I18N_H__
#define __I18N_H__

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// The active language's strings, read from languages/<code>.json next to the
// binary. Keys are the English text; only the active language stays in memory.
// Like LVGL, this is only safe to use while holding lv_lock.
namespace I18n {

struct Language {
  std::string code;
  std::string name;
};

// text in the active language, or text itself if it has no translation. Copy
// the result: switching language frees it.
const char *tr(const char *text);

// A button map that follows the language, for LVGL widgets that keep the
// pointer. Stays valid for the life of the program.
const char **map(const char *const *english);

// English first, then by name. English is listed even with no files installed.
std::vector<Language> available(const std::string &dir);

const std::string &current();
size_t loaded_strings();

// Reads the file before touching the active language, so a bad file changes nothing.
bool load(const std::string &dir, const std::string &code);

// Every translated string to the English it came from, for retranslating
// widgets. Call before load().
std::vector<std::pair<std::string, std::string>> reverse_table();

// languages/ next to the running binary
std::string default_dir();

// Switch language and retranslate everything on screen (i18n_ui.cpp)
bool switch_to(const std::string &code);

}  // namespace I18n

#endif  // __I18N_H__
