#include "hh_backend.h"
#include "state.h"
#include "logger.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>

// How often weights may be re-asked for while a print is consuming them. Grams
// move slowly and the panel shows them rounded, so this is about keeping the
// number honest, not watching it tick.
static constexpr auto WEIGHT_REFRESH = std::chrono::minutes(5);

// Happy Hare's action strings (mmu_controller._get_action_string) onto the
// neutral activity. Everything that is not resting blocks filament motion, so
// anything unrecognised maps to Moving rather than Idle.
//
// HH has no action of its own for a toolchange -- it runs through Unloading
// and Loading -- so a pending next_tool is what says "swapping".
static MmuActivity hh_activity(const std::string &action, bool changing, bool faulted) {
  if (faulted) return MmuActivity::Error;
  if (changing) return MmuActivity::Swapping;
  if (action.empty() || action == "Idle") return MmuActivity::Idle;
  if (action == "Loading" || action == "Loading Ext") return MmuActivity::Loading;
  if (action == "Unloading" || action == "Exiting Ext") return MmuActivity::Unloading;
  // Forming/Cutting Tip, Cutting Filament, Purging, Heating, Checking, Homing,
  // Selecting, Preload, Unknown
  return MmuActivity::Moving;
}

// "RRGGBB" from HH's per-gate colour. gate_color may be a w3c colour name
// ("indigo"), so prefer the pre-resolved gate_color_rgb float tuple; an
// unset colour stays "" (its rgb tuple would read as black).
static std::string gate_hex_colour(const json &gate_colour, const json &gate_colour_rgb, int g) {
  std::string name;
  if (gate_colour.is_array() && g < (int)gate_colour.size() && gate_colour[g].is_string()) {
    name = gate_colour[g].template get<std::string>();
  }
  if (name.empty()) return "";

  if (gate_colour_rgb.is_array() && g < (int)gate_colour_rgb.size() &&
      gate_colour_rgb[g].is_array() && gate_colour_rgb[g].size() == 3) {
    int rgb[3];
    for (int i = 0; i < 3; i++) {
      double v = gate_colour_rgb[g][i].is_number() ? gate_colour_rgb[g][i].template get<double>() : 0.0;
      rgb[i] = std::max(0, std::min(255, (int)std::lround(v * 255.0)));
    }
    return fmt::format("{:02X}{:02X}{:02X}", rgb[0], rgb[1], rgb[2]);
  }

  if (!name.empty() && name[0] == '#') name = name.substr(1);
  if (name.size() == 6 &&
      std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isxdigit(c); })) {
    return name;
  }
  return "";
}

// json::value() returns by value, deep-copying the whole array every refresh.
// These are read-only, so hand back a reference into State instead.
static const json &array_at(const json &obj, const char *key) {
  static const json empty = json::array();
  const auto it = obj.find(key);
  return (it != obj.end() && it->is_array()) ? *it : empty;
}

// a string field, "" when absent or the wrong type
static std::string str_at(const json &obj, const char *key) {
  const auto it = obj.find(key);
  return (it != obj.end() && it->is_string()) ? it->template get<std::string>() : std::string();
}

static int int_at(const json &obj, const char *key, int fallback) {
  const auto it = obj.find(key);
  return (it != obj.end() && it->is_number()) ? it->template get<int>() : fallback;
}

bool HhBackend::detect() {
  json &objs = State::get_instance()->get_data("/printer_objs/objects"_json_pointer);
  if (!objs.is_array()) return false;
  bool found = std::any_of(objs.begin(), objs.end(), [](const json &o) {
    return o.is_string() && o.template get<std::string>() == "mmu";
  });
  if (found) {
    // re-runs on every klipper reconnect; nothing cached survives a reconfig
    spool_weights.clear();
    fetched_spool_ids.clear();
  }
  return found;
}

// Pure predicate: this runs on the websocket thread before the UI lock is
// taken, so it must not touch any member the UI thread can reach.
bool HhBackend::owns_update(json &j) {
  auto &status = j["/params/0"_json_pointer];
  return status.is_object() && status.contains("mmu");
}

void HhBackend::refresh() {
  State *state = State::get_instance();
  json &mmu = state->get_data("/printer_state/mmu"_json_pointer);

  slots.clear();
  loaded_slot = -1;
  activity = MmuActivity::Idle;
  message = "";
  message_error = false;
  error = false;
  bypass = false;
  in_print = false;
  paused = false;
  enabled = true;
  filament_loaded = false;

  if (!mmu.is_object()) return;

  const int num_gates = int_at(mmu, "num_gates", 0);
  if (num_gates <= 0) return;

  const json &ttg = array_at(mmu, "ttg_map");
  const json &gate_status = array_at(mmu, "gate_status");
  const json &gate_material = array_at(mmu, "gate_material");
  const json &gate_colour = array_at(mmu, "gate_color");
  const json &gate_colour_rgb = array_at(mmu, "gate_color_rgb");
  const json &gate_spool_id = array_at(mmu, "gate_spool_id");
  const json &es_groups = array_at(mmu, "endless_spool_groups");

  const int cur_gate = int_at(mmu, "gate", -1);
  const int cur_tool = int_at(mmu, "tool", -1);
  // HH reports the destination in `gate` once a toolchange starts, so ignore
  // it while one is in flight or the panel shows the incoming spool as loaded
  const bool changing = int_at(mmu, "next_tool", -1) >= 0;
  const bool tool_loaded = str_at(mmu, "filament") == "Loaded";
  filament_loaded = tool_loaded;
  const std::string action = str_at(mmu, "action");
  // MMU ENABLE=0 makes HH refuse every command; absent means enabled
  const auto en = mmu.find("enabled");
  enabled = !(en != mmu.end() && en->is_boolean() && !en->template get<bool>());
  // print_state is HH's own job state machine (mmu_print_state_machine):
  // initialized | ready | started | printing | complete | cancelled | error |
  // pause_locked | paused | standby | idle. is_paused/is_locked/is_in_print
  // are deprecated aliases of it. pause_locked is the moment between an MMU
  // fault and its pause macro finishing; both mean "paused by the MMU".
  const std::string print_state = str_at(mmu, "print_state");
  in_print = print_state == "started" || print_state == "printing";
  paused = print_state == "paused" || print_state == "pause_locked";

  const std::string spoolman_mode = str_at(mmu, "spoolman_support");
  spoolman = !spoolman_mode.empty() && spoolman_mode != "off";
  // In pull mode the spoolman database is the source of truth for the gate map
  // and HH refuses local edits; in push mode the gate map is, so they are fine.
  const bool editable = spoolman_mode != "pull";

  // endless_spool_enabled is the current name; endless_spool is the
  // deprecated alias HH still publishes for older clients
  bool es_enabled = false;
  for (const char *key : {"endless_spool_enabled", "endless_spool"}) {
    const auto it = mmu.find(key);
    if (it == mmu.end()) continue;
    es_enabled = (it->is_boolean() && it->template get<bool>()) ||
                 (it->is_number() && it->template get<int>() != 0);
    break;
  }

  for (int g = 0; g < num_gates; g++) {
    MmuSlot slot;
    slot.name = fmt::format("Gate {}", g);

    if (ttg.is_array()) {
      for (size_t t = 0; t < ttg.size(); t++) {
        if (ttg[t].is_number() && ttg[t].template get<int>() == g) {
          if (!slot.map.empty()) slot.map += ",";
          slot.map += fmt::format("T{}", t);
        }
      }
    }

    if (gate_material.is_array() && g < (int)gate_material.size() &&
        gate_material[g].is_string()) {
      slot.material = gate_material[g].template get<std::string>();
    }
    slot.colour = gate_hex_colour(gate_colour, gate_colour_rgb, g);

    int status = -1;
    if (gate_status.is_array() && g < (int)gate_status.size() && gate_status[g].is_number()) {
      status = gate_status[g].template get<int>();
    }
    // GATE_UNKNOWN -1, GATE_EMPTY 0, GATE_AVAILABLE 1, _FROM_BUFFER 2.
    // Unknown means filament was detected but never confirmed, and HH will
    // still try to load it, so it counts as present but not confirmed ready.
    slot.prepped = status != 0;
    slot.ready = status > 0;
    slot.tool_loaded = !changing && (g == cur_gate) && tool_loaded;
    slot.can_configure = editable;

    if (gate_spool_id.is_array() && g < (int)gate_spool_id.size() &&
        gate_spool_id[g].is_number()) {
      const int sid = gate_spool_id[g].template get<int>();
      const auto w = sid > 0 ? spool_weights.find(sid) : spool_weights.end();
      if (w != spool_weights.end()) slot.weight = w->second;
    }

    slots.push_back(slot);
  }

  // endless spool groups -> per-slot backup: the next gate in the same group
  if (es_enabled && es_groups.is_array() && (int)es_groups.size() >= num_gates) {
    for (int g = 0; g < num_gates; g++) {
      if (!es_groups[g].is_number()) continue;
      const int grp = es_groups[g].template get<int>();
      for (int step = 1; step < num_gates; step++) {
        const int other = (g + step) % num_gates;
        if (es_groups[other].is_number() && es_groups[other].template get<int>() == grp) {
          slots[g].backup = other;
          break;
        }
      }
    }
  }

  if (!changing && tool_loaded && cur_gate >= 0 && cur_gate < num_gates) {
    loaded_slot = cur_gate;
  }

  // An MMU fault mid-print pauses the printer, and the user has to fix it and
  // RESUME before anything else. print_state "error" is not that: it mirrors
  // klipper's own job error, nothing of HH's clears it, so it is not a banner.
  error = paused;
  message_error = error;
  if (error) {
    // reason_for_pause carries the actual failure text while HH is paused
    message = str_at(mmu, "reason_for_pause");
    if (message.empty()) message = "MMU paused";
  } else if (!enabled) {
    message = "MMU disabled";
  }
  bypass = cur_tool == -2; // TOOL_GATE_BYPASS
  activity = hh_activity(action, changing, error);

  // Refresh spoolman weights whenever the set of assigned spool ids changes.
  // That covers every way the map itself can move, so the only reason to ask
  // again on a clock is consumption -- printing eats grams without touching
  // the map -- and that is gated on the print. An idle printer asks once and
  // then stays quiet.
  //
  // A fetch that came back empty retries on the same clock whether or not a
  // print is running: spoolman being unreachable should not leave the weights
  // blank until something else happens to move.
  if (spoolman) {
    const auto now = std::chrono::steady_clock::now();
    std::vector<int> ids;
    if (gate_spool_id.is_array()) {
      for (auto &sid : gate_spool_id) {
        if (sid.is_number() && sid.template get<int>() > 0) {
          ids.push_back(sid.template get<int>());
        }
      }
    }
    const bool overdue = (in_print || fetch_failed) && now - last_fetch > WEIGHT_REFRESH;
    if (!ids.empty() && (ids != fetched_spool_ids || overdue)) {
      fetched_spool_ids = ids;
      last_fetch = now;
      fetch_spoolman_weights();
    }
  }
}

void HhBackend::fetch_spoolman_weights() {
  json params = {
    { "request_method", "GET" },
    { "path", "/v1/spool" },  // archived spools are not on a gate
  };
  ws.send_jsonrpc("server.spoolman.proxy", params, [this](json &d) {
    auto &spools = d["/result"_json_pointer];
    if (!spools.is_array()) {
      // leave fetched_spool_ids in place: clearing it here re-fired this
      // request on every status frame while spoolman was unreachable. The
      // retry is the clock in refresh(), which this flag keeps running even
      // when no print is consuming anything.
      fetch_failed = true;
      return;
    }
    fetch_failed = false;
    for (auto &s : spools) {
      if (s.contains("id") && s["id"].is_number()) {
        int grams = 0;
        if (s.contains("remaining_weight") && s["remaining_weight"].is_number()) {
          grams = (int)std::lround(s["remaining_weight"].template get<double>());
        }
        spool_weights[s["id"].template get<int>()] = grams;
      }
    }
    if (changed) changed(); // ids unchanged, so the re-refresh can't re-fetch
  });
}

bool HhBackend::can_load(int slot) const {
  // HH will attempt a gate it has not confirmed (GATE_UNKNOWN), but not an
  // empty one. With bypass filament in the extruder every gate load is refused
  // until MMU_UNLOAD, which is the console's job -- no slot owns that filament.
  return motion_ok() && !(bypass && filament_loaded) && valid(slot) &&
         slots[slot].prepped && !slots[slot].tool_loaded;
}

bool HhBackend::can_unload() const {
  return motion_ok() && loaded_slot >= 0;
}

bool HhBackend::can_eject(int slot) const {
  return motion_ok() && valid(slot) && slots[slot].prepped;
}

bool HhBackend::can_set_backup(int slot) const {
  // MMU_ENDLESS_SPOOL only rewrites the group list: no motion, so no reason to
  // refuse it mid-print or while paused. MMU ENABLE=0 does refuse it, and only
  // logs the refusal, so nothing would come back to tell the panel.
  return enabled && valid(slot) && slots.size() > 1;
}

// One verb, both jobs: MMU_CHANGE_TOOL runs HH's full toolchange sequence and
// handles the "nothing loaded yet" case itself.
void HhBackend::load(int slot) {
  if (!valid(slot)) return;
  if (!slots[slot].map.empty()) {
    // prefer the mapped tool: MMU_CHANGE_TOOL GATE= refuses a gate no tool
    // maps to ("No tool associated with gate")
    json &mmu = State::get_instance()->get_data("/printer_state/mmu"_json_pointer);
    const json &ttg = array_at(mmu, "ttg_map");
    for (size_t t = 0; t < ttg.size(); t++) {
      if (ttg[t].is_number() && ttg[t].template get<int>() == slot) {
        ws.gcode_script(fmt::format("MMU_CHANGE_TOOL TOOL={}", t));
        return;
      }
    }
  }
  // Unmapped gate: select it by hand. MMU_SELECT refuses while filament is
  // loaded, and MMU_UNLOAD with nothing loaded logs "Filament not loaded" and
  // returns without raising, so this covers a fresh load and a swap alike.
  ws.gcode_script(fmt::format("MMU_UNLOAD\nMMU_SELECT GATE={}\nMMU_LOAD", slot));
}

void HhBackend::unload() {
  ws.gcode_script("MMU_UNLOAD");
}

void HhBackend::eject(int slot) {
  ws.gcode_script(fmt::format("MMU_EJECT GATE={}", slot));
}

// One MMU_GATE_MAP edit. Every attribute left out keeps its value except TEMP,
// which falls back to the printer default, so the gate's own temperature goes
// back with each edit or a colour change would silently reset it.
void HhBackend::gate_map(int slot, const std::string &args) {
  json &mmu = State::get_instance()->get_data("/printer_state/mmu"_json_pointer);
  const json &temps = array_at(mmu, "gate_temperature");
  std::string temp;
  if (slot < (int)temps.size() && temps[slot].is_number()) {
    temp = fmt::format(" TEMP={}", temps[slot].template get<int>());
  }
  ws.gcode_script(fmt::format("MMU_GATE_MAP GATE={} {}{}", slot, args, temp));
}

void HhBackend::set_colour(int slot, const std::string &hex) {
  if (!valid(slot)) return;
  // validate_color takes 'rrggbb', a colour name, or an empty string to clear
  gate_map(slot, fmt::format("COLOR={}", hex));
}

void HhBackend::set_material(int slot, const std::string &material) {
  if (!valid(slot)) return;
  gate_map(slot, fmt::format("MATERIAL={}", KWebSocketClient::quote_arg(material)));
}

std::vector<int> HhBackend::current_groups() const {
  State *state = State::get_instance();
  const json es = state->get_data("/printer_state/mmu/endless_spool_groups"_json_pointer);
  const json ng = state->get_data("/printer_state/mmu/num_gates"_json_pointer);
  const int n = ng.is_number() ? ng.template get<int>() : 0;
  std::vector<int> groups;
  for (int i = 0; i < n; i++) {
    groups.push_back(es.is_array() && i < (int)es.size() && es[i].is_number()
                     ? es[i].template get<int>() : i);
  }
  return groups;
}

void HhBackend::send_groups(const std::vector<int> &groups) {
  std::string csv;
  for (size_t i = 0; i < groups.size(); i++) {
    if (i) csv += ",";
    csv += std::to_string(groups[i]);
  }
  // endless_spool defaults to off, so assigning a backup has to turn it on or
  // the groups are stored and never acted on
  ws.gcode_script(fmt::format("MMU_ENDLESS_SPOOL ENABLE=1 GROUPS={}", csv));
}

// the panel's pairwise backup -> endless spool groups
void HhBackend::set_backup(int slot, int backup) {
  std::vector<int> groups = current_groups();
  if ((int)groups.size() <= slot) return;

  if (backup < 0) {
    // The panel clears "X backs up slot" by calling this on slot. Groups are
    // cycles, so X is the gate after slot, and X is what leaves the group --
    // moving slot instead would leave X covering slot's predecessor. Former
    // partners keep each other.
    const int out = valid(slot) && slots[slot].backup >= 0 ? slots[slot].backup : slot;
    groups[out] = *std::max_element(groups.begin(), groups.end()) + 1;
  } else {
    if (backup >= (int)groups.size()) return;
    groups[backup] = groups[slot];
  }
  send_groups(groups);
}

void HhBackend::reset_failure() {
  // HH's own instruction on a fault: "After fixing, call RESUME to continue
  // printing". Its RESUME wrapper clears the pause state; MMU_UNLOCK only
  // restores the extruder temperature and MMU_RECOVER only resyncs filament
  // position, and neither ends the pause.
  ws.gcode_script("RESUME");
}
