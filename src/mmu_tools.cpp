#include "mmu_tools.h"

#include <algorithm>
#include <cctype>
#include <cmath>

std::string mmu_normalise_hex(const std::string &colour) {
  if (colour.empty() || colour == "NONE") return "";
  std::string s = colour[0] == '#' ? colour.substr(1) : colour;
  std::transform(s.begin(), s.end(), s.begin(), ::toupper);
  return s;
}

bool mmu_same_material(const std::string &a, const std::string &b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
           return std::toupper(x) == std::toupper(y);
         });
}

static bool hex_rgb(const std::string &colour, int rgb[3]) {
  const std::string hex = mmu_normalise_hex(colour);
  if (hex.size() < 6) return false;
  try {
    const unsigned long v = std::stoul(hex.substr(0, 6), nullptr, 16);
    rgb[0] = (v >> 16) & 0xFF;
    rgb[1] = (v >> 8) & 0xFF;
    rgb[2] = v & 0xFF;
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

// sRGB -> CIE Lab under D65
static void lab(const int rgb[3], double out[3]) {
  double lin[3];
  for (int i = 0; i < 3; i++) {
    const double c = rgb[i] / 255.0;
    lin[i] = c > 0.04045 ? std::pow((c + 0.055) / 1.055, 2.4) : c / 12.92;
  }
  const double xyz[3] = {
    (lin[0] * 0.4124 + lin[1] * 0.3576 + lin[2] * 0.1805) / 0.95047,
    lin[0] * 0.2126 + lin[1] * 0.7152 + lin[2] * 0.0722,
    (lin[0] * 0.0193 + lin[1] * 0.1192 + lin[2] * 0.9505) / 1.08883,
  };
  double f[3];
  for (int i = 0; i < 3; i++) {
    f[i] = xyz[i] > 0.008856 ? std::cbrt(xyz[i]) : 7.787 * xyz[i] + 16.0 / 116.0;
  }
  out[0] = 116 * f[1] - 16;
  out[1] = 500 * (f[0] - f[1]);
  out[2] = 200 * (f[1] - f[2]);
}

double mmu_colour_distance(const std::string &a, const std::string &b) {
  int x[3], y[3];
  if (!hex_rgb(a, x) || !hex_rgb(b, y)) return -1;
  double p[3], q[3];
  lab(x, p);
  lab(y, q);
  return std::sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) +
                   (p[2] - q[2]) * (p[2] - q[2]));
}

bool mmu_has_filament(const MmuSlot &slot) {
  return slot.prepped || slot.ready || slot.tool_loaded;
}

int mmu_slot_for_tool(const std::vector<MmuSlot> &slots, int tool) {
  const std::string want = "T" + std::to_string(tool);
  for (size_t i = 0; i < slots.size(); i++) {
    // whole entries of "T0,T1", so T1 never matches T10
    std::string entry;
    for (char ch : slots[i].map + ",") {
      if (ch != ',') { if (ch != ' ') entry += ch; continue; }
      if (mmu_same_material(entry, want)) return (int)i;
      entry.clear();
    }
  }
  return -1;
}

// a slicer list field: an array, a JSON array in a string, or "a;b" (as
// Mainsail's convertStringToArray reads it)
static std::vector<std::string> string_list(const json &j) {
  std::vector<std::string> out;
  if (j.is_array()) {
    for (const auto &v : j) out.push_back(v.is_string() ? v.template get<std::string>() : "");
    return out;
  }
  if (!j.is_string()) return out;
  const std::string s = j.template get<std::string>();
  if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
    const json parsed = json::parse(s, nullptr, false);
    if (parsed.is_array()) return string_list(parsed);
  }
  size_t start = 0;
  while (start <= s.size()) {
    size_t end = s.find(';', start);
    if (end == std::string::npos) end = s.size();
    std::string v = s.substr(start, end - start);
    v.erase(0, v.find_first_not_of(" \t\""));
    v.erase(v.find_last_not_of(" \t\"") + 1);
    out.push_back(v);
    start = end + 1;
  }
  return out;
}

std::vector<MmuFileTool> mmu_file_tools(const json &metadata) {
  std::vector<MmuFileTool> out;
  const auto res = metadata.find("result");
  if (res == metadata.end() || !res->is_object()) return out;

  const auto w = res->find("filament_weights");
  if (w == res->end() || !w->is_array()) return out;

  // extruder_colors for slicers that write no filament_colors
  std::vector<std::string> colours;
  for (const char *key : {"filament_colors", "extruder_colors"}) {
    const auto c = res->find(key);
    if (c != res->end()) colours = string_list(*c);
    if (!colours.empty()) break;
  }
  const auto t = res->find("filament_type");
  const std::vector<std::string> types = t != res->end() ? string_list(*t)
                                                         : std::vector<std::string>();

  for (size_t i = 0; i < w->size(); i++) {
    const json &g = (*w)[i];
    if (!g.is_number() || g.template get<double>() <= 0) continue;
    MmuFileTool tool;
    tool.tool = (int)i;
    tool.grams = g.template get<double>();
    if (i < colours.size()) tool.colour = mmu_normalise_hex(colours[i]);
    if (i < types.size()) tool.material = types[i];
    out.push_back(tool);
  }
  return out;
}
