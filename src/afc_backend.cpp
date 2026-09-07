#include "afc_backend.h"
#include "state.h"
#include "logger.h"

#include <algorithm>
#include <cctype>
#include <map>

bool AfcBackend::detect() {
  json &objs = State::get_instance()->get_data("/printer_objs/objects"_json_pointer);
  if (!objs.is_array()) return false;
  return std::any_of(objs.begin(), objs.end(), [](const json &o) {
    return o.is_string() && o.template get<std::string>() == "AFC";
  });
}

bool AfcBackend::owns_update(json &j) {
  auto &status = j["/params/0"_json_pointer];
  if (!status.is_object()) return false;
  for (auto &el : status.items()) {
    if (el.key().rfind("AFC", 0) == 0) return true;
  }
  return false;
}

// AFC's current_state is a closed enum (AFC.py State): Initialized, Idle,
// Error, Loading, Unloading, ToolSwap, ToolDock, ToolPickup, Ejecting, Moving,
// Restoring. Anything outside the resting states means the unit is mid-
// operation, so an unrecognised one maps to Moving rather than Idle.
static MmuActivity afc_activity(const std::string &state, bool error) {
  if (error || state == "Error") return MmuActivity::Error;
  if (state.empty() || state == "Idle" || state == "Initialized") return MmuActivity::Idle;
  if (state == "Loading") return MmuActivity::Loading;
  if (state == "Unloading") return MmuActivity::Unloading;
  if (state == "ToolSwap") return MmuActivity::Swapping;
  if (state == "Ejecting") return MmuActivity::Ejecting;
  return MmuActivity::Moving;
}

void AfcBackend::refresh() {
  State *state = State::get_instance();
  json &afc = state->get_data("/printer_state/AFC"_json_pointer);

  slots.clear();
  lane_ids.clear();
  loaded_slot = -1;
  activity = MmuActivity::Idle;
  message = "";
  message_error = false;
  error = false;
  bypass = false;

  // AFC drives its own toolchanges during a print; nothing the panel offers
  // may cut into that
  json &pstat = state->get_data("/printer_state/print_stats/state"_json_pointer);
  printing = pstat.is_string() && pstat.template get<std::string>() == "printing";

  if (!afc.is_object()) return;

  std::string current_load;
  auto &load_j = afc["/current_load"_json_pointer];
  if (load_j.is_string()) current_load = load_j.template get<std::string>();

  // message is a {message, type} pair. Reading it with a nested json pointer
  // would throw if AFC ever published something else there, and a throw on the
  // websocket thread takes the UI down, so resolve it a level at a time.
  auto msg_it = afc.find("message");
  if (msg_it != afc.end() && msg_it->is_object()) {
    auto text = msg_it->find("message");
    if (text != msg_it->end() && text->is_string()) message = text->template get<std::string>();
    // AFC tags queued messages "error" or "warning"; only the former is a fault
    auto type = msg_it->find("type");
    message_error = type != msg_it->end() && type->is_string() &&
                    type->template get<std::string>() == "error";
  }

  auto &err = afc["/error_state"_json_pointer];
  if (err.is_boolean()) error = err.template get<bool>();

  auto &byp = afc["/bypass_state"_json_pointer];
  if (byp.is_boolean()) bypass = byp.template get<bool>();

  auto &cur_state = afc["/current_state"_json_pointer];
  activity = afc_activity(cur_state.is_string() ? cur_state.template get<std::string>() : "", error);
  if (error) message_error = true;

  // AFC reports spoolman as a bool (or a URL string in some versions)
  auto &spm = afc["/spoolman"_json_pointer];
  spoolman = (spm.is_boolean() && spm.template get<bool>()) ||
             (spm.is_string() && !spm.template get<std::string>().empty());

  // Lane state lives in a per-lane klipper object whose name AFC does not
  // report ("AFC_lane lane1", "AFC_stepper lane1", ... across versions), so
  // index the object list once by the name after the space rather than
  // re-scanning it for every lane
  std::map<std::string, std::string> lane_objs;
  json &objects = state->get_data("/printer_objs/objects"_json_pointer);
  if (objects.is_array()) {
    for (auto &o : objects) {
      if (!o.is_string()) continue;
      const std::string obj_name = o.template get<std::string>();
      if (obj_name.rfind("AFC_", 0) != 0) continue;
      auto space = obj_name.find(' ');
      if (space == std::string::npos) continue;
      const std::string suffix = obj_name.substr(space + 1);
      if (lane_objs.count(suffix)) continue;
      json &st = state->get_data(json::json_pointer(fmt::format("/printer_state/{}", obj_name)));
      // hubs, extruders and buffers share the naming; only a lane reports these
      if (st.is_object() && (st.contains("load") || st.contains("prep"))) {
        lane_objs[suffix] = obj_name;
      }
    }
  }

  std::vector<std::string> runout; // per-slot runout lane name, resolved below
  auto &lane_names = afc["/lanes"_json_pointer];
  if (lane_names.is_array()) {
    for (auto &l : lane_names) {
      if (!l.is_string()) continue;
      const std::string lane_name = l.template get<std::string>();
      auto obj = lane_objs.find(lane_name);
      if (obj == lane_objs.end()) continue;

      json &st = state->get_data(json::json_pointer(fmt::format("/printer_state/{}", obj->second)));
      MmuSlot slot;
      // "lane12" reads awkwardly as a title; show "Lane 12". Custom names,
      // which AFC also allows, pass through untouched.
      slot.name = lane_name;
      if (lane_name.rfind("lane", 0) == 0 && lane_name.size() > 4 &&
          std::all_of(lane_name.begin() + 4, lane_name.end(),
                      [](unsigned char c) { return std::isdigit(c); })) {
        slot.name = fmt::format("Lane {}", lane_name.substr(4));
      }
      std::string runout_lane;
      // AFC published `map` as a string until 1.2.x, then as a list of the
      // T(n) macros mapped to the lane. Accept both.
      if (st.contains("map")) {
        const json &m = st["map"];
        if (m.is_string()) {
          slot.map = m.template get<std::string>();
        } else if (m.is_array()) {
          for (const auto &t : m) {
            if (!t.is_string()) continue;
            if (!slot.map.empty()) slot.map += ",";
            slot.map += t.template get<std::string>();
          }
        }
      }
      if (st.contains("runout_lane") && st["runout_lane"].is_string()) runout_lane = st["runout_lane"].template get<std::string>();
      if (st.contains("material") && st["material"].is_string()) slot.material = st["material"].template get<std::string>();
      if (st.contains("color") && st["color"].is_string()) {
        slot.colour = st["color"].template get<std::string>();
        if (!slot.colour.empty() && slot.colour[0] == '#') slot.colour.erase(0, 1);
      }
      if (st.contains("prep") && st["prep"].is_boolean()) slot.prepped = st["prep"].template get<bool>();
      // `load` is AFC's own loadable test (AFC_lane.load_state): the lane's
      // load switch on a physical hub, loaded_to_hub on a virtual one. It is
      // exactly what TOOL_LOAD refuses on, so `ready` follows it and nothing
      // else -- loaded_to_hub can stay latched on a lane whose spool has run
      // out, and offering a load there just manufactures a lane failure.
      if (st.contains("load") && st["load"].is_boolean()) slot.ready = st["load"].template get<bool>();
      if (st.contains("tool_loaded") && st["tool_loaded"].is_boolean()) slot.tool_loaded = st["tool_loaded"].template get<bool>();
      if (st.contains("weight") && st["weight"].is_number()) slot.weight = st["weight"].template get<int>();
      // A lane with a spoolman spool assigned has its colour and material
      // refetched from spoolman at every PREP, so a local edit here would
      // apply and then quietly revert. Spoolman owns it; edit it there.
      if (spoolman && st.contains("spool_id") && st["spool_id"].is_number()) {
        slot.can_configure = st["spool_id"].template get<int>() < 0;
      }

      slots.push_back(slot);
      lane_ids.push_back(lane_name);
      runout.push_back(runout_lane);
    }
  }

  for (size_t i = 0; i < slots.size(); i++) {
    if (lane_ids[i] == current_load) loaded_slot = (int)i;
    if (runout[i].empty()) continue;
    for (size_t b = 0; b < slots.size(); b++) {
      if (lane_ids[b] == runout[i]) { slots[i].backup = (int)b; break; }
    }
  }
}

bool AfcBackend::can_load(int slot) const {
  // TOOL_LOAD needs the lane loaded to its switch -- a spool sitting on PREP
  // alone is not loadable and AFC raises a lane failure if asked.
  //
  // AFC also wants the hub clear, but only reaches that check after unloading
  // whatever is in the tool, and a hub reads occupied precisely while a lane
  // is loaded through it. Testing it here would grey out every swap, so it is
  // left to AFC, which refuses with "Hub not clear when trying to load".
  return motion_ok() && valid(slot) && slots[slot].ready && !slots[slot].tool_loaded;
}

bool AfcBackend::can_unload() const {
  return motion_ok() && loaded_slot >= 0;
}

bool AfcBackend::can_eject(int slot) const {
  // LANE_UNLOAD refuses the lane that is loaded to the extruder
  return motion_ok() && valid(slot) && !slots[slot].tool_loaded &&
         (slots[slot].prepped || slots[slot].ready);
}

bool AfcBackend::can_set_backup(int slot) const {
  // SET_RUNOUT only writes a lane-to-lane pointer: no filament needed, and no
  // reason to refuse it mid-print or mid-fault, which is exactly when a spool
  // is noticed running low
  return valid(slot) && slots.size() > 1;
}

void AfcBackend::load(int slot) {
  if (!valid(slot)) return;
  // CHANGE_TOOL unloads whatever is in the tool first; TOOL_LOAD assumes it is
  // already empty and errors otherwise
  if (loaded_slot >= 0 && loaded_slot != slot) {
    ws.gcode_script(fmt::format("CHANGE_TOOL LANE={}", lane_id(slot)));
  } else {
    ws.gcode_script(fmt::format("TOOL_LOAD LANE={}", lane_id(slot)));
  }
}

void AfcBackend::unload() {
  ws.gcode_script("TOOL_UNLOAD");
}

void AfcBackend::eject(int slot) {
  if (!valid(slot)) return;
  ws.gcode_script(fmt::format("LANE_UNLOAD LANE={}", lane_id(slot)));
}

void AfcBackend::set_colour(int slot, const std::string &hex) {
  if (!valid(slot)) return;
  ws.gcode_script(fmt::format("SET_COLOR LANE={} COLOR={}", lane_id(slot), hex));
}

void AfcBackend::set_material(int slot, const std::string &material) {
  if (!valid(slot)) return;
  ws.gcode_script(fmt::format("SET_MATERIAL LANE={} MATERIAL={}",
                              lane_id(slot), KWebSocketClient::quote_arg(material)));
}

void AfcBackend::set_backup(int slot, int backup) {
  if (!valid(slot) || (backup >= 0 && !valid(backup))) return;
  if (backup < 0) {
    ws.gcode_script(fmt::format("SET_RUNOUT LANE={} RUNOUT=NONE", lane_id(slot)));
  } else {
    ws.gcode_script(fmt::format("SET_RUNOUT LANE={} RUNOUT={}", lane_id(slot), lane_id(backup)));
  }
}

void AfcBackend::reset_failure() {
  ws.gcode_script("RESET_FAILURE");
}

// AFC exposes message[0] of a queue it only pops on request, so an
// unacknowledged message hides every one behind it.
void AfcBackend::dismiss_message() {
  ws.gcode_script("AFC_CLEAR_MESSAGE");
}
