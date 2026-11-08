#include "console_colour.h"

#include <algorithm>
#include <cctype>

ConsoleLine console_line_kind(const std::string &line) {
  auto starts = [&line](const char *prefix) { return line.rfind(prefix, 0) == 0; };
  if (starts("!!")) return ConsoleLine::ERROR;
  if (starts("> ")) return ConsoleLine::SENT;
  if (starts("// action:")) return ConsoleLine::PROMPT;
  if (starts("echo:")) return ConsoleLine::ECHO;
  std::string lower(line);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  if (lower.find("warning") != std::string::npos) return ConsoleLine::WARNING;
  return ConsoleLine::PLAIN;
}

std::string colourize_console(const std::string &text,
                              const std::function<std::string(ConsoleLine)> &colour) {
  std::string out;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    std::string c = line.empty() ? "" : colour(console_line_kind(line));
    size_t from = 0;
    while (true) {
      size_t hash = line.find('#', from);
      std::string part = line.substr(from, hash == std::string::npos ? std::string::npos : hash - from);
      if (!part.empty()) out += c.empty() ? part : "#" + c + " " + part + "#";
      if (hash == std::string::npos) break;
      out += "##";
      from = hash + 1;
    }
    if (end == std::string::npos) break;
    out += '\n';
    start = end + 1;
  }
  return out;
}
