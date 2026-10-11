#include "mmu_tools.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <tuple>

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

std::vector<int> mmu_automap(const std::vector<MmuFileTool> &tools,
                             const std::vector<MmuSlot> &slots,
                             const std::vector<int> &current,
                             bool share, bool weights) {
  std::vector<int> target(tools.size(), MMU_AUTOMAP_CLEAR);
  std::vector<bool> taken(slots.size(), false);
  std::vector<double> used(slots.size(), 0);

  auto fits = [&](size_t t, size_t s) {
    const MmuFileTool &tool = tools[t];
    const MmuSlot &slot = slots[s];
    if (!mmu_has_filament(slot)) return false;
    if (!tool.material.empty() && !mmu_same_material(tool.material, slot.material)) return false;
    const double d = mmu_colour_distance(tool.colour, slot.colour);
    if (!tool.colour.empty() && (d < 0 || d > MMU_COLOUR_CLOSE)) return false;
    return !weights || slot.weight <= 0 || slot.weight >= used[s] + tool.grams;
  };
  auto distance = [&](size_t t, size_t s) {
    return std::max(0.0, mmu_colour_distance(tools[t].colour, slots[s].colour));
  };
  auto take = [&](size_t t, size_t s) {
    target[t] = (int)s;
    taken[s] = true;
    used[s] += tools[t].grams;
  };

  // nearest colour first, a tool's current slot winning a tie; a slot each
  std::vector<std::tuple<double, bool, size_t, size_t>> pairs;
  for (size_t t = 0; t < tools.size(); t++) {
    for (size_t s = 0; s < slots.size(); s++) {
      const bool moves = t >= current.size() || current[t] != (int)s;
      if (fits(t, s)) pairs.emplace_back(distance(t, s), moves, t, s);
    }
  }
  std::sort(pairs.begin(), pairs.end());
  for (const auto &p : pairs) {
    const size_t t = std::get<2>(p), s = std::get<3>(p);
    if (target[t] == MMU_AUTOMAP_CLEAR && !taken[s]) take(t, s);
  }

  // then, where the vendor allows it, share a slot that fits
  for (size_t t = 0; share && t < tools.size(); t++) {
    if (target[t] != MMU_AUTOMAP_CLEAR) continue;
    int best = -1;
    for (size_t s = 0; s < slots.size(); s++) {
      if (taken[s] && fits(t, s) && (best < 0 || distance(t, s) < distance(t, best))) best = (int)s;
    }
    if (best >= 0) take(t, best);
  }

  for (size_t t = 0; t < tools.size() && t < current.size(); t++) {
    if (target[t] == current[t]) target[t] = -1;  // already there
  }
  return target;
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
