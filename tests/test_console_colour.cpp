// The console's colouring: line kinds, and that LVGL draws exactly the original
// text with the right colour on every character.
#include "console_colour.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      failures++;                                                         \
    }                                                                     \
  } while (0)

// LVGL 8.3's recolour parser (_lv_txt_is_cmd in lv_txt.c) and how the label uses
// it: the characters it draws, each with the colour it is drawn in ("" default).
struct Drawn {
  std::string text;
  std::vector<std::string> colour;
};

static Drawn lvgl_draw(const std::string &s) {
  enum { WAIT, PAR, IN } state = WAIT;
  Drawn d;
  std::string param, current;
  for (char c : s) {
    bool cmd = false;
    if (c == '#') {
      if (state == WAIT) { state = PAR; cmd = true; param.clear(); }
      else if (state == PAR) state = WAIT;   // ## : a drawn #
      else { state = WAIT; cmd = true; current.clear(); }
    }
    if (state == PAR) {
      if (c == ' ') { state = IN; current = param; }
      else if (c != '#') param += c;
      cmd = true;
    }
    if (!cmd) {
      d.text += c;
      d.colour.push_back(state == IN ? current : "");
    }
  }
  return d;
}

static std::string colour_of(ConsoleLine kind) {
  switch (kind) {
    case ConsoleLine::ERROR: return "ff0000";
    case ConsoleLine::SENT: return "0000ff";
    case ConsoleLine::PROMPT: return "888888";
    case ConsoleLine::ECHO: return "00ff00";
    case ConsoleLine::WARNING: return "ffaa00";
    default: return "";
  }
}

// every character of every line drawn, in its line's colour
static void check_round_trip(const std::string &text) {
  Drawn d = lvgl_draw(colourize_console(text, colour_of));
  CHECK(d.text == text);
  if (d.text != text) {
    std::fprintf(stderr, "  drew %s\n  want %s\n", d.text.c_str(), text.c_str());
    return;
  }
  size_t line_start = 0;
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '\n') {
      line_start = i + 1;
      continue;
    }
    size_t end = text.find('\n', line_start);
    std::string line = text.substr(line_start, end == std::string::npos ? std::string::npos : end - line_start);
    // LVGL draws a literal # only outside a colour command: that glyph keeps
    // the default colour, the text around it its line's
    CHECK(d.colour[i] == (text[i] == '#' ? "" : colour_of(console_line_kind(line))));
  }
}

int main() {
  CHECK(console_line_kind("!! Move out of range") == ConsoleLine::ERROR);
  CHECK(console_line_kind("> G28") == ConsoleLine::SENT);
  CHECK(console_line_kind("// action:prompt_show") == ConsoleLine::PROMPT);
  CHECK(console_line_kind("echo: hello") == ConsoleLine::ECHO);
  CHECK(console_line_kind("// Warning: probe reading noisy") == ConsoleLine::WARNING);
  CHECK(console_line_kind("// Klipper state: Ready") == ConsoleLine::PLAIN);
  CHECK(console_line_kind("") == ConsoleLine::PLAIN);

  CHECK(colourize_console("!! bad", colour_of) == "#ff0000 !! bad#");
  CHECK(colourize_console("// plain", colour_of) == "// plain");

  check_round_trip("!! Move out of range\n> G28\n// Klipper state: Ready\n");
  check_round_trip("> SET_LED COLOR=#ff00ff\n");          // # inside a coloured line
  check_round_trip("#start and end#\n");                 // # at the edges, uncoloured
  check_round_trip("!! #1 failed ## twice #\n");         // several, coloured
  check_round_trip("echo: ###\n\n\nwarning: a#b\n");     // runs of #, empty lines
  check_round_trip("no newline at the end");

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("console colour tests passed\n");
  return 0;
}
