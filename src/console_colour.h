#ifndef __CONSOLE_COLOUR_H__
#define __CONSOLE_COLOUR_H__

#include <functional>
#include <string>

// What a console line is, from its Klipper prefix.
enum class ConsoleLine { PLAIN, ERROR, SENT, PROMPT, ECHO, WARNING, INFO, SECONDARY };

ConsoleLine console_line_kind(const std::string &line);

// Translate plugin span colours and line kinds to LVGL recolour commands.
// Explicit #RGB/#RRGGBB colours override semantic classes, which use COLOUR.
// Nested/multiline spans apply within this message; malformed unclosed spans
// cannot affect the next message. Bold tags are removed without bold styling.
// With colours disabled, remove supported tags but emit plain text. A # in the
// text comes out as ##, which LVGL shows as # only outside a command, so a
// coloured line is closed before each # and opened again after it.
std::string colourize_console(const std::string &text,
                              const std::function<std::string(ConsoleLine)> &colour,
                              bool colours = true);

#endif
