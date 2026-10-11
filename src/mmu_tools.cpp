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

// a map's T(n) entries, whitespace trimmed
static std::vector<std::string> map_entries(const std::string &map) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= map.size()) {
    size_t end = map.find(',', start);
    if (end == std::string::npos) end = map.size();
    std::string entry = map.substr(start, end - start);
    entry.erase(0, entry.find_first_not_of(' '));
    entry.erase(entry.find_last_not_of(' ') + 1);
    if (!entry.empty()) out.push_back(entry);
    start = end + 1;
  }
  return out;
}

int mmu_slot_for_tool(const std::vector<MmuSlot> &slots, int tool) {
  const std::string want = "T" + std::to_string(tool);
  for (size_t i = 0; i < slots.size(); i++) {
    // map is "T0" or "T0,T1"; compare whole entries, so T1 never matches T10
    for (const auto &e : map_entries(slots[i].map)) {
      if (mmu_same_material(e, want)) return (int)i;
    }
  }
  return -1;
}

namespace {
// One matching pass over a fixed set of pinned tools. -1 = stays.
struct Matcher {
  const std::vector<MmuFileTool> &tools;
  const std::vector<MmuSlot> &slots;
  const std::vector<int> &cur;
  bool share, weights;

  std::vector<int> target;
  std::vector<bool> placed, taken;
  std::vector<double> used;  // grams the print takes from each slot so far

  // the slot has the grams left for this tool on top of what it already
  // gives; a weight not known (no spoolman, or none reported) never refuses
  bool enough(size_t t, size_t s) const {
    return !weights || slots[s].weight <= 0 || slots[s].weight >= used[s] + tools[t].grams;
  }

  // The only slots automap will pick: loaded, the same material (a slot with
  // none set is not known to be), a colour close enough, and the grams. A
  // file that names no material or colour has nothing to mismatch.
  bool candidate(size_t t, size_t s) const {
    const MmuFileTool &tool = tools[t];
    const MmuSlot &slot = slots[s];
    if (!mmu_has_filament(slot)) return false;
    if (!tool.material.empty() && !mmu_same_material(tool.material, slot.material)) return false;
    if (!tool.colour.empty()) {
      const double d = mmu_colour_distance(tool.colour, slot.colour);
      if (d < 0 || d > MMU_COLOUR_CLOSE) return false;
    }
    return enough(t, s);
  }

  // as good as it gets: a candidate in all but the same colour
  bool exact(size_t t, size_t s) const {
    return candidate(t, s) &&
           (tools[t].colour.empty() || mmu_colour_distance(tools[t].colour, slots[s].colour) <= MMU_COLOUR_MISMATCH);
  }

  double distance(size_t t, size_t s) const {
    const double d = mmu_colour_distance(tools[t].colour, slots[s].colour);
    return d < 0 ? 0 : d;
  }

  void put(size_t t, size_t s, bool move) {
    placed[t] = taken[s] = true;
    used[s] += tools[t].grams;
    if (move) target[t] = (int)s;
  }

  std::vector<int> run(const std::vector<bool> &pinned) {
    target.assign(tools.size(), -1);
    placed.assign(tools.size(), false);
    taken.assign(slots.size(), false);
    used.assign(slots.size(), 0);

    // pinned tools, and tools already on an exact match, stay
    for (size_t t = 0; t < tools.size(); t++) {
      const int c = cur[t];
      if (c < 0 || (taken[c] && !share)) continue;
      if (pinned[t] || exact(t, c)) put(t, c, false);
    }

    // free candidate slots, nearest colour first; a slot to each tool
    std::vector<std::tuple<double, size_t, size_t>> pairs;
    for (size_t t = 0; t < tools.size(); t++) {
      if (placed[t]) continue;
      for (size_t s = 0; s < slots.size(); s++) {
        if (!taken[s] && candidate(t, s)) pairs.emplace_back(distance(t, s), t, s);
      }
    }
    std::sort(pairs.begin(), pairs.end());
    for (const auto &p : pairs) {
      const size_t t = std::get<1>(p), s = std::get<2>(p);
      if (!placed[t] && !taken[s] && candidate(t, s)) put(t, s, true);
    }

    // where the vendor lets tools share a slot, one already in use that is a
    // candidate for this tool too -- a slot to itself is preferred, but not
    // over a match
    if (share) {
      for (size_t t = 0; t < tools.size(); t++) {
        if (placed[t]) continue;
        int best = -1;
        for (size_t s = 0; s < slots.size(); s++) {
          if (taken[s] && candidate(t, s) && (best < 0 || distance(t, s) < distance(t, best))) best = (int)s;
        }
        if (best >= 0) put(t, best, cur[t] != best);
      }
    }

    // no match is forced: the rest are left for the user to pick
    for (size_t t = 0; t < tools.size(); t++) {
      if (!placed[t]) target[t] = MMU_AUTOMAP_CLEAR;
    }
    return target;
  }
};
}  // namespace

std::vector<int> mmu_automap(const std::vector<MmuFileTool> &tools,
                             const std::vector<MmuSlot> &slots,
                             const std::vector<int> &current,
                             bool share, bool weights) {
  std::vector<int> cur(tools.size(), -1);
  for (size_t t = 0; t < tools.size() && t < current.size(); t++) {
    if (current[t] >= 0 && (size_t)current[t] < slots.size()) cur[t] = current[t];
  }
  Matcher m{tools, slots, cur, share, weights, {}, {}, {}, {}};

  // A tool already on a candidate slot is never left without one: if the
  // pass would clear it, or (where slots are not shared) give its slot to
  // another tool and so swap it onto whatever that one left, it is pinned
  // where it is and the pass rerun. Pins only grow, so this settles.
  std::vector<bool> pinned(tools.size(), false);
  std::vector<int> target;
  for (bool changed = true; changed;) {
    changed = false;
    target = m.run(pinned);
    m.used.assign(slots.size(), 0);  // candidate() below asks as if the slot were empty
    for (size_t u = 0; u < tools.size(); u++) {
      if (pinned[u] || cur[u] < 0 || !m.candidate(u, cur[u])) continue;
      bool ok = target[u] != MMU_AUTOMAP_CLEAR;
      if (ok && target[u] < 0 && !share) {
        for (size_t t = 0; t < tools.size(); t++) {
          if (t != u && target[t] == cur[u]) ok = false;
        }
      }
      if (!ok) {
        pinned[u] = true;
        changed = true;
      }
    }
  }

  // a tool that is already where it belongs needs no command
  for (size_t t = 0; t < tools.size(); t++) {
    if (target[t] >= 0 && cur[t] == target[t]) target[t] = -1;
  }
  return target;
}

// A slicer list field, as moonraker hands it over: a real array, a JSON array
// packed into a string, or a ';'-separated string. Mainsail's
// convertStringToArray reads it the same way.
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

  // filament_colors describes the filaments; extruder_colors is the fallback
  // for slicers that only write per-extruder colours
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
