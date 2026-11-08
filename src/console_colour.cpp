#include "console_colour.h"

#include <algorithm>
#include <cctype>
#include <vector>

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

namespace {
std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return value;
}

bool space(char c) { return std::isspace(static_cast<unsigned char>(c)); }

std::string trim(const std::string &value) {
  size_t first = 0, end = value.size();
  while (first < end && space(value[first])) ++first;
  while (end > first && space(value[end - 1])) --end;
  return value.substr(first, end - first);
}

struct Tag {
  size_t end;
  bool span, closing;
  std::string classes, style;
};

// Only recognise the small subset emitted by AFC and Happy Hare. Comparisons,
// gcode parameters and unknown tags remain literal console text.
bool read_tag(const std::string &text, size_t start, Tag &tag) {
  if (text[start] != '<') return false;
  size_t pos = start + 1;
  tag = {};
  if (pos < text.size() && text[pos] == '/') { tag.closing = true; ++pos; }
  size_t name_start = pos;
  while (pos < text.size() && std::isalpha(static_cast<unsigned char>(text[pos]))) ++pos;
  std::string name = lower(text.substr(name_start, pos - name_start));
  if (name != "span" && name != "b") return false;
  tag.span = name == "span";
  if (pos == text.size() || (!space(text[pos]) && text[pos] != '>')) return false;
  while (pos < text.size()) {
    while (pos < text.size() && space(text[pos])) ++pos;
    if (pos == text.size()) return false;
    if (text[pos] == '>') { tag.end = pos + 1; return true; }
    if (tag.closing) return false;
    size_t key_start = pos;
    while (pos < text.size() && !space(text[pos]) && text[pos] != '=' && text[pos] != '>') ++pos;
    std::string key = lower(text.substr(key_start, pos - key_start));
    while (pos < text.size() && space(text[pos])) ++pos;
    if (pos == text.size()) return false;
    if (text[pos] != '=') continue;  // a boolean attribute
    ++pos;
    while (pos < text.size() && space(text[pos])) ++pos;
    if (pos == text.size()) return false;
    char quote = (text[pos] == '\'' || text[pos] == '"') ? text[pos++] : 0;
    size_t value_start = pos;
    while (pos < text.size() && (quote ? text[pos] != quote : !space(text[pos]) && text[pos] != '>')) ++pos;
    std::string value = text.substr(value_start, pos - value_start);
    if (quote) {
      if (pos == text.size()) return false;
      ++pos;
    }
    if (key == "class") tag.classes = value;
    if (key == "style") tag.style = value;
  }
  return false;
}

std::string span_colour(const Tag &tag,
                        const std::function<std::string(ConsoleLine)> &colour) {
  size_t start = 0;
  while (start < tag.style.size()) {
    size_t end = tag.style.find(';', start);
    std::string property = tag.style.substr(start, end == std::string::npos ? end : end - start);
    size_t colon = property.find(':');
    if (colon != std::string::npos && lower(trim(property.substr(0, colon))) == "color") {
      std::string value = trim(property.substr(colon + 1));
      if ((value.size() == 4 || value.size() == 7) && value[0] == '#' &&
          std::all_of(value.begin() + 1, value.end(), [](unsigned char c) { return std::isxdigit(c); })) {
        value = lower(value.substr(1));
        if (value.size() == 3) {
          value = std::string(2, value[0]) + std::string(2, value[1]) + std::string(2, value[2]);
        }
        return value;
      }
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  std::string classes = lower(tag.classes);
  start = 0;
  while (start < classes.size()) {
    while (start < classes.size() && space(classes[start])) ++start;
    size_t end = start;
    while (end < classes.size() && !space(classes[end])) ++end;
    std::string name = classes.substr(start, end - start);
    if (name == "success--text") return colour(ConsoleLine::ECHO);
    if (name == "warning--text" || name == "warn--text") return colour(ConsoleLine::WARNING);
    if (name == "error--text" || name == "danger--text") return colour(ConsoleLine::ERROR);
    if (name == "primary--text") return colour(ConsoleLine::SENT);
    if (name == "secondary--text") return colour(ConsoleLine::SECONDARY);
    if (name == "info--text") return colour(ConsoleLine::INFO);
    start = end;
  }
  return "";
}

void append_run(std::string &out, const std::string &text, const std::string &c, bool colours) {
  if (!colours) { out += text; return; }
  size_t from = 0;
  while (true) {
    size_t hash = text.find('#', from);
    std::string part = text.substr(from, hash == std::string::npos ? hash : hash - from);
    if (!part.empty()) out += c.empty() ? part : "#" + c + " " + part + "#";
    if (hash == std::string::npos) break;
    out += "##";
    from = hash + 1;
  }
}
} // namespace

std::string colourize_console(const std::string &text,
                              const std::function<std::string(ConsoleLine)> &colour,
                              bool colours) {
  std::string out;
  std::vector<std::string> spans;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    std::string line = text.substr(start, end == std::string::npos ? end : end - start);
    // Sent gcode is literal, even if it happens to contain HTML-looking text.
    bool sent = line.rfind("> ", 0) == 0;
    std::string visible;
    Tag tag;
    for (size_t i = 0; i < line.size();) {
      if (!sent && read_tag(line, i, tag)) i = tag.end;
      else visible += line[i++];
    }
    std::string base = visible.empty() ? "" : colour(console_line_kind(visible));
    auto active = [&]() {
      if (!sent) {
        for (auto i = spans.rbegin(); i != spans.rend(); ++i) if (!i->empty()) return *i;
      }
      return base;
    };
    std::string run;
    for (size_t i = 0; i < line.size();) {
      if (!sent && read_tag(line, i, tag)) {
        append_run(out, run, active(), colours);
        run.clear();
        if (tag.span) {
          if (tag.closing) { if (!spans.empty()) spans.pop_back(); }
          else spans.push_back(span_colour(tag, colour));
        }
        i = tag.end;
      } else run += line[i++];
    }
    append_run(out, run, active(), colours);
    if (end == std::string::npos) break;
    out += '\n';
    start = end + 1;
  }
  return out;
}
