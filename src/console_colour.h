#ifndef __CONSOLE_COLOUR_H__
#define __CONSOLE_COLOUR_H__

#include <functional>
#include <string>

// What a console line is, from its Klipper prefix.
enum class ConsoleLine { PLAIN, ERROR, SENT, PROMPT, ECHO, WARNING };

ConsoleLine console_line_kind(const std::string &line);

// The text with each line wrapped in an LVGL recolour command (#rrggbb text#),
// with the colour COLOUR gives its kind; "" leaves a line uncoloured. A # in the
// text comes out as ##, which LVGL shows as # only outside a command, so a
// coloured line is closed before each # and opened again after it.
std::string colourize_console(const std::string &text,
                              const std::function<std::string(ConsoleLine)> &colour);

#endif
