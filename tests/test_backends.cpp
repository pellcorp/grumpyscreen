// test_backends.cpp
//
// Exercises an MmuBackend against synthetic klipper status: what it makes of
// the vendor's fields, what it allows, and the gcode it sends.
//
// Only the backend under test is linked. State and the two KWebSocketClient
// methods the verbs reach for are defined here instead, so the fixture needs
// no display, no config file and no moonraker.

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "afc_backend.h"
#ifdef MMU_BACKEND_HH
#include "hh_backend.h"
#endif
#include "state.h"
#include "websocket_client.h"

// ---------------------------------------------------------------------------
// stand-ins for the parts of the app a backend talks to
// ---------------------------------------------------------------------------
static std::vector<std::string> sent;   // gcode the backend handed to moonraker

State *State::instance = NULL;
std::mutex State::lock;

State::State(std::mutex &state_lock) : NotifyConsumer(state_lock) {}

State *State::get_instance() {
  if (instance == NULL) instance = new State(State::lock);
  return instance;
}

void State::reset() { data.clear(); }

void State::set_data(const std::string &key, json &j, const std::string &json_path) {
  auto patch = j[json::json_pointer(json_path)];
  if (!patch.is_null()) data[key].merge_patch(patch);
}

json &State::get_data() { return data; }
json &State::get_data(const json::json_pointer &ptr) { return data[ptr]; }
void State::consume(json &j) { set_data("printer_state", j, "/params/0"); }

KWebSocketClient::KWebSocketClient(hv::EventLoopPtr loop)
  : hv::WebSocketClient(loop), id(0) {}
KWebSocketClient::~KWebSocketClient() {}

int KWebSocketClient::gcode_script(const std::string &gcode) {
  sent.push_back(gcode);
  return 0;
}

int KWebSocketClient::send_jsonrpc(const std::string &, const json &,
                                   std::function<void(json&)>) {
  return 0;
}

// ---------------------------------------------------------------------------
// checks
// ---------------------------------------------------------------------------
static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::cout << "FAIL line " << __LINE__ << ": " #cond << std::endl;   \
      failures++;                                                         \
    }                                                                     \
  } while (0)

#define CHECK_EQ(actual, expected)                                        \
  do {                                                                    \
    const auto _a = (actual);                                             \
    const auto _e = (expected);                                           \
    if (!(_a == _e)) {                                                    \
      std::cout << "FAIL line " << __LINE__ << ": " #actual " == "        \
                << _a << ", expected " << _e << std::endl;                \
      failures++;                                                         \
    }                                                                     \
  } while (0)

// ---------------------------------------------------------------------------
// synthetic AFC status, shaped like the real thing: one AFC object listing its
// lanes by name, plus a klipper object per lane
// ---------------------------------------------------------------------------
static std::string tool_name(int index) {
  return "T" + std::to_string(index);
}

static json afc_lane(const std::string &name, int index) {
  return {
    {"name", name}, {"unit", "Turtle_1"}, {"hub", "Turtle_1"},
    {"extruder", "extruder"}, {"buffer", "TN"}, {"lane", index},
    {"map", json::array({tool_name(index)})},
    {"current_map", tool_name(index)},
    {"load", false}, {"prep", false}, {"tool_loaded", false},
    {"loaded_to_hub", false}, {"material", ""}, {"color", ""},
    {"spool_id", nullptr}, {"weight", 0}, {"runout_lane", nullptr},
    {"filament_status", "Not Ready"}, {"dist_hub", 60},
  };
}

static json afc_status() {
  json j;
  j["printer_objs"]["objects"] = json::array({
    "print_stats", "AFC", "AFC_hub Turtle_1", "AFC_extruder extruder",
    "AFC_lane lane1", "AFC_lane lane2", "AFC_lane lane3",
  });
  j["printer_state"]["print_stats"]["state"] = "standby";
  j["printer_state"]["AFC"] = {
    {"current_load", nullptr}, {"current_lane", nullptr}, {"next_lane", nullptr},
    {"current_state", "Idle"}, {"error_state", false}, {"bypass_state", false},
    {"spoolman", nullptr},
    {"lanes", json::array({"lane1", "lane2", "lane3"})},
    {"message", {{"message", ""}, {"type", ""}}},
  };
  // a hub and an extruder share the AFC_ prefix and the naming shape; neither
  // should be mistaken for a lane
  j["printer_state"]["AFC_hub Turtle_1"] = {{"state", false}, {"lanes", json::array({"lane1"})}};
  j["printer_state"]["AFC_extruder extruder"] = {{"lane_loaded", nullptr}, {"tool_start", "buffer"}};
  j["printer_state"]["AFC_lane lane1"] = afc_lane("lane1", 0);
  j["printer_state"]["AFC_lane lane2"] = afc_lane("lane2", 1);
  j["printer_state"]["AFC_lane lane3"] = afc_lane("lane3", 2);
  return j;
}

// load a whole tree, replacing anything held before
static void load_state(json tree) {
  State *state = State::get_instance();
  state->reset();
  state->set_data("printer_objs", tree, "/printer_objs");
  state->set_data("printer_state", tree, "/printer_state");
}

// ---------------------------------------------------------------------------

static void test_lane_mapping(KWebSocketClient &ws) {
  json j = afc_status();
  json &lane1 = j["printer_state"]["AFC_lane lane1"];
  json &lane2 = j["printer_state"]["AFC_lane lane2"];
  json &lane3 = j["printer_state"]["AFC_lane lane3"];

  lane1["prep"] = true;
  lane1["load"] = true;
  lane1["loaded_to_hub"] = true;
  lane1["tool_loaded"] = true;
  lane1["material"] = "PLA";
  lane1["color"] = "#FF0000";      // AFC stores colours with the hash
  lane1["weight"] = 950;
  lane1["runout_lane"] = "lane3";  // lane1 runs out -> lane3 takes over
  lane1["map"] = json::array({"T0", "T4"});
  j["printer_state"]["AFC"]["current_load"] = "lane1";

  lane2["prep"] = true;            // on the prep sensor only: present, not loadable
  lane2["load"] = false;
  lane2["loaded_to_hub"] = true;   // ... even with the hub latch still set
  lane2["material"] = "PETG";
  lane2["color"] = "00FF00";       // and sometimes without

  lane3["prep"] = true;
  lane3["load"] = true;
  lane3["name"] = "Spare";         // AFC allows custom lane names
  j["printer_state"]["AFC"]["lanes"] = json::array({"lane1", "lane2", "lane3"});

  load_state(j);
  AfcBackend afc(ws);
  CHECK(afc.detect());
  afc.refresh();

  CHECK_EQ(afc.slots.size(), (size_t)3);
  if (afc.slots.size() != 3) return;

  // "lane1" reads awkwardly as a title, a custom name passes through
  CHECK_EQ(afc.slots[0].name, std::string("Lane 1"));
  CHECK_EQ(afc.slots[2].name, std::string("Lane 3"));

  CHECK_EQ(afc.slots[0].map, std::string("T0,T4"));
  CHECK_EQ(afc.slots[1].map, std::string("T1"));
  CHECK_EQ(afc.slots[0].colour, std::string("FF0000")); // hash stripped
  CHECK_EQ(afc.slots[1].colour, std::string("00FF00"));
  CHECK_EQ(afc.slots[0].material, std::string("PLA"));
  CHECK_EQ(afc.slots[0].weight, 950);

  CHECK(afc.slots[0].prepped && afc.slots[0].ready && afc.slots[0].tool_loaded);
  // the point of the load/loaded_to_hub distinction: a lane whose spool has
  // run out keeps its hub latch, and AFC's TOOL_LOAD still refuses it
  CHECK(afc.slots[1].prepped);
  CHECK(!afc.slots[1].ready);
  CHECK_EQ(afc.loaded_slot, 0);
  // runout_lane -> a slot index
  CHECK_EQ(afc.slots[0].backup, 2);
  CHECK_EQ(afc.slots[1].backup, -1);
}

static void test_activity(KWebSocketClient &ws) {
  struct Case { const char *state; MmuActivity activity; };
  const Case cases[] = {
    {"Idle", MmuActivity::Idle},
    {"Initialized", MmuActivity::Idle},
    {"Loading", MmuActivity::Loading},
    {"Unloading", MmuActivity::Unloading},
    {"ToolSwap", MmuActivity::Swapping},
    {"Ejecting", MmuActivity::Ejecting},
    {"ToolDock", MmuActivity::Moving},
    {"Restoring", MmuActivity::Moving},
    {"Error", MmuActivity::Error},
    // AFC gaining a state we have never heard of must not read as resting
    {"Recalibrating", MmuActivity::Moving},
  };

  for (const Case &c : cases) {
    json j = afc_status();
    j["printer_state"]["AFC"]["current_state"] = c.state;
    load_state(j);
    AfcBackend afc(ws);
    afc.refresh();
    CHECK(afc.activity == c.activity);
    CHECK_EQ(afc.busy(), c.activity != MmuActivity::Idle);
  }

  // error_state on its own is a fault, whatever the state string says
  json j = afc_status();
  j["printer_state"]["AFC"]["error_state"] = true;
  load_state(j);
  AfcBackend afc(ws);
  afc.refresh();
  CHECK(afc.error);
  CHECK(afc.activity == MmuActivity::Error);
  CHECK(afc.busy());
}

static void test_messages(KWebSocketClient &ws) {
  // a warning is information, and is not a fault
  json j = afc_status();
  j["printer_state"]["AFC"]["message"] = {{"message", "lane3 runout"}, {"type", "warning"}};
  load_state(j);
  {
    AfcBackend afc(ws);
    afc.refresh();
    CHECK_EQ(afc.message, std::string("lane3 runout"));
    CHECK(!afc.message_error);
    CHECK(!afc.error);
  }

  j["printer_state"]["AFC"]["message"] = {{"message", "hub not clear"}, {"type", "error"}};
  load_state(j);
  {
    AfcBackend afc(ws);
    afc.refresh();
    CHECK(afc.message_error);
  }

  // a fault with no queued text still reads as a fault
  j["printer_state"]["AFC"]["message"] = {{"message", ""}, {"type", ""}};
  j["printer_state"]["AFC"]["error_state"] = true;
  load_state(j);
  {
    AfcBackend afc(ws);
    afc.refresh();
    CHECK(afc.message.empty());
    CHECK(afc.message_error);
  }
}

static void test_spoolman(KWebSocketClient &ws) {
  json j = afc_status();
  j["printer_state"]["AFC"]["spoolman"] = "http://mainsail.local:7912";
  j["printer_state"]["AFC_lane lane1"]["prep"] = true;
  j["printer_state"]["AFC_lane lane1"]["load"] = true;
  j["printer_state"]["AFC_lane lane1"]["spool_id"] = 42;
  load_state(j);
  AfcBackend afc(ws);
  afc.refresh();
  CHECK(afc.spoolman);
  // spoolman owns an assigned lane's metadata: AFC refetches it at PREP, so a
  // local edit would apply and then revert
  CHECK(!afc.slots[0].can_configure);
  CHECK(afc.slots[1].can_configure);

  // no spoolman server, no ownership and no meaningful weights
  j["printer_state"]["AFC"]["spoolman"] = nullptr;
  load_state(j);
  AfcBackend off(ws);
  off.refresh();
  CHECK(!off.spoolman);
  CHECK(off.slots[0].can_configure);
}

static void test_permissions(KWebSocketClient &ws) {
  json j = afc_status();
  j["printer_state"]["AFC_lane lane1"]["prep"] = true;
  j["printer_state"]["AFC_lane lane1"]["load"] = true;
  j["printer_state"]["AFC_lane lane1"]["tool_loaded"] = true;
  j["printer_state"]["AFC"]["current_load"] = "lane1";
  j["printer_state"]["AFC_lane lane2"]["prep"] = true;   // present, not fed
  j["printer_state"]["AFC_lane lane3"]["prep"] = true;
  j["printer_state"]["AFC_lane lane3"]["load"] = true;   // fed and loadable

  load_state(j);
  {
    AfcBackend afc(ws);
    afc.refresh();
    CHECK(!afc.can_load(0));       // already in the tool
    CHECK(!afc.can_load(1));       // prep only -- TOOL_LOAD would fail
    CHECK(afc.can_load(2));
    CHECK(afc.can_unload());
    CHECK(!afc.can_eject(0));      // LANE_UNLOAD refuses the loaded lane
    CHECK(afc.can_eject(1));
    CHECK(afc.can_set_backup(0));
    CHECK(!afc.can_load(99));      // out of range
    CHECK(!afc.can_eject(-1));
    // AFC's SET_COLOR cannot express "no colour"
    CHECK(!afc.can_clear_colour());
  }

  // AFC drives its own toolchanges during a print; nothing else may cut in
  j["printer_state"]["print_stats"]["state"] = "printing";
  load_state(j);
  {
    AfcBackend afc(ws);
    afc.refresh();
    CHECK(!afc.can_load(2));
    CHECK(!afc.can_unload());
    CHECK(!afc.can_eject(1));
    CHECK(afc.can_set_backup(0));  // wiring a backup is not motion
  }

  // a fault blocks motion until it is reset
  j["printer_state"]["print_stats"]["state"] = "standby";
  j["printer_state"]["AFC"]["error_state"] = true;
  load_state(j);
  {
    AfcBackend afc(ws);
    afc.refresh();
    CHECK(!afc.can_load(2));
    CHECK(!afc.can_unload());
    CHECK(!afc.can_eject(1));
    CHECK(afc.can_set_backup(0));
  }

  // so does the bypass
  j["printer_state"]["AFC"]["error_state"] = false;
  j["printer_state"]["AFC"]["bypass_state"] = true;
  load_state(j);
  {
    AfcBackend afc(ws);
    afc.refresh();
    CHECK(afc.bypass);
    CHECK(!afc.can_load(2));
    CHECK(!afc.can_unload());
  }

  // one lane, nothing to fall back to
  json single = afc_status();
  single["printer_state"]["AFC"]["lanes"] = json::array({"lane1"});
  load_state(single);
  {
    AfcBackend afc(ws);
    afc.refresh();
    CHECK_EQ(afc.slots.size(), (size_t)1);
    CHECK(!afc.can_set_backup(0));
  }
}

static void test_verbs(KWebSocketClient &ws) {
  json j = afc_status();
  j["printer_state"]["AFC_lane lane1"]["prep"] = true;
  j["printer_state"]["AFC_lane lane1"]["load"] = true;
  j["printer_state"]["AFC_lane lane3"]["prep"] = true;
  j["printer_state"]["AFC_lane lane3"]["load"] = true;
  load_state(j);

  AfcBackend afc(ws);
  afc.refresh();

  // nothing loaded: a plain load
  sent.clear();
  afc.load(0);
  CHECK_EQ(sent.size(), (size_t)1);
  CHECK_EQ(sent[0], std::string("TOOL_LOAD LANE=lane1"));

  // something else loaded: CHANGE_TOOL, which unloads it first
  j["printer_state"]["AFC_lane lane1"]["tool_loaded"] = true;
  j["printer_state"]["AFC"]["current_load"] = "lane1";
  load_state(j);
  afc.refresh();
  sent.clear();
  afc.load(2);
  CHECK_EQ(sent[0], std::string("CHANGE_TOOL LANE=lane3"));

  // the same slot that is already loaded is a plain load, not a swap
  sent.clear();
  afc.load(0);
  CHECK_EQ(sent[0], std::string("TOOL_LOAD LANE=lane1"));

  sent.clear();
  afc.unload();
  CHECK_EQ(sent[0], std::string("TOOL_UNLOAD"));

  sent.clear();
  afc.eject(2);
  CHECK_EQ(sent[0], std::string("LANE_UNLOAD LANE=lane3"));

  sent.clear();
  afc.set_colour(1, "1A2B3C");
  CHECK_EQ(sent[0], std::string("SET_COLOR LANE=lane2 COLOR=1A2B3C"));

  sent.clear();
  afc.set_material(1, "PETG");
  CHECK_EQ(sent[0], std::string("SET_MATERIAL LANE=lane2 MATERIAL=PETG"));

  // klipper splits extended parameters with shlex, so anything it would eat
  // has to be quoted on the way out
  sent.clear();
  afc.set_material(1, "PLA Silk");
  CHECK_EQ(sent[0], std::string("SET_MATERIAL LANE=lane2 MATERIAL=\"PLA Silk\""));
  sent.clear();
  afc.set_material(1, "PLA #2");
  CHECK_EQ(sent[0], std::string("SET_MATERIAL LANE=lane2 MATERIAL=\"PLA #2\""));

  sent.clear();
  afc.set_backup(0, 2);
  CHECK_EQ(sent[0], std::string("SET_RUNOUT LANE=lane1 RUNOUT=lane3"));

  sent.clear();
  afc.set_backup(0, -1);
  CHECK_EQ(sent[0], std::string("SET_RUNOUT LANE=lane1 RUNOUT=NONE"));

  sent.clear();
  afc.reset_failure();
  CHECK_EQ(sent[0], std::string("RESET_FAILURE"));

  // AFC keeps messages in a queue it only pops on request
  sent.clear();
  afc.dismiss_message();
  CHECK_EQ(sent[0], std::string("AFC_CLEAR_MESSAGE"));

  // an out-of-range slot sends nothing rather than a command naming no lane
  sent.clear();
  afc.load(99);
  afc.eject(-1);
  afc.set_colour(7, "FFFFFF");
  afc.set_material(7, "PLA");
  afc.set_backup(0, 7);
  CHECK(sent.empty());
}

// A vendor renaming a field, or changing its type between versions, must not
// take the UI down: refresh() runs on the websocket thread, where a json type
// error is fatal.
static void test_hostile_status(KWebSocketClient &ws) {
  const char *lane_fields[] = {"map", "color", "material", "prep", "load",
                               "tool_loaded", "loaded_to_hub", "weight",
                               "spool_id", "runout_lane"};

  for (const char *field : lane_fields) {
    for (int shape = 0; shape < 3; shape++) {
      json j = afc_status();
      json &lane = j["printer_state"]["AFC_lane lane1"];
      switch (shape) {
        case 0: lane[field] = 17; break;                      // number
        case 1: lane[field] = "surprise"; break;              // string
        case 2: lane[field] = json::array({1, 2}); break;     // array
      }
      load_state(j);
      AfcBackend afc(ws);
      try {
        afc.refresh();
        (void)afc.can_load(0);
      } catch (const std::exception &e) {
        std::cout << "FAIL threw on lane." << field << " shape " << shape
                  << ": " << e.what() << std::endl;
        failures++;
      }
    }
  }

  const char *afc_fields[] = {"current_load", "current_state", "error_state",
                              "bypass_state", "spoolman", "lanes", "message"};
  for (const char *field : afc_fields) {
    for (int shape = 0; shape < 3; shape++) {
      json j = afc_status();
      json &afc_obj = j["printer_state"]["AFC"];
      switch (shape) {
        case 0: afc_obj[field] = 17; break;
        case 1: afc_obj[field] = "surprise"; break;
        case 2: afc_obj[field] = json::array({1, 2}); break;
      }
      load_state(j);
      AfcBackend afc(ws);
      try {
        afc.refresh();
        (void)afc.can_unload();
      } catch (const std::exception &e) {
        std::cout << "FAIL threw on AFC." << field << " shape " << shape
                  << ": " << e.what() << std::endl;
        failures++;
      }
    }
  }

  // AFC present but nothing else: no slots, nothing enabled, no crash
  json bare;
  bare["printer_objs"]["objects"] = json::array({"AFC"});
  bare["printer_state"]["AFC"] = json::object();
  load_state(bare);
  AfcBackend afc(ws);
  CHECK(afc.detect());
  afc.refresh();
  CHECK(afc.slots.empty());
  CHECK_EQ(afc.loaded_slot, -1);
  CHECK(afc.activity == MmuActivity::Idle);
  CHECK(!afc.can_unload());

  // AFC absent entirely
  json none;
  none["printer_objs"]["objects"] = json::array({"print_stats", "extruder"});
  load_state(none);
  AfcBackend gone(ws);
  CHECK(!gone.detect());
  gone.refresh();
  CHECK(gone.slots.empty());
}

#ifdef MMU_BACKEND_HH
// ---------------------------------------------------------------------------
// synthetic Happy Hare status: one "mmu" object with per-gate arrays
// ---------------------------------------------------------------------------
static json hh_status() {
  json mmu = {
    {"enabled", true}, {"num_gates", 4}, {"print_state", "ready"},
    {"tool", -1}, {"gate", -1}, {"next_tool", -1},
    {"filament", "Unloaded"}, {"action", "Idle"}, {"reason_for_pause", ""},
    {"spoolman_support", "off"},
    {"ttg_map", {0, 1, 2, 3}},
    {"gate_status", {1, 0, -1, 2}},  // available, empty, unknown, from buffer
    {"gate_material", {"PLA", "", "PETG", "ABS"}},
    {"gate_color", {"ff0000", "", "indigo", "00ff00"}},
    {"gate_color_rgb", {{1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.294, 0.0, 0.51}, {0.0, 1.0, 0.0}}},
    {"gate_spool_id", {-1, -1, -1, -1}},
    {"gate_temperature", {210, 200, 240, 250}},
    {"endless_spool_groups", {0, 1, 2, 3}},
    {"endless_spool_enabled", true},
  };
  json j;
  j["printer_objs"]["objects"] = {"mmu", "toolhead"};
  j["printer_state"]["mmu"] = mmu;
  return j;
}

static void test_hh_gate_mapping(KWebSocketClient &ws) {
  json j = hh_status();
  json &mmu = j["printer_state"]["mmu"];
  mmu["ttg_map"] = {0, 0, 2, 3};             // T0 and T1 both on gate 0, gate 1 unmapped
  mmu["gate"] = 0;
  mmu["tool"] = 0;
  mmu["filament"] = "Loaded";
  mmu["endless_spool_groups"] = {0, 0, 1, 1};
  load_state(j);

  HhBackend hh(ws);
  CHECK(hh.detect());
  hh.refresh();

  CHECK_EQ(hh.slots.size(), (size_t)4);
  if (hh.slots.size() != 4) return;

  CHECK_EQ(hh.slots[0].name, std::string("Gate 0"));
  CHECK_EQ(hh.slots[0].map, std::string("T0,T1"));
  CHECK_EQ(hh.slots[1].map, std::string(""));
  CHECK_EQ(hh.slots[2].map, std::string("T2"));

  CHECK_EQ(hh.slots[0].colour, std::string("FF0000"));
  CHECK_EQ(hh.slots[1].colour, std::string(""));       // unset stays unset, not black
  CHECK_EQ(hh.slots[2].colour, std::string("4B0082")); // w3c name via the rgb tuple
  CHECK_EQ(hh.slots[3].colour, std::string("00FF00"));
  CHECK_EQ(hh.slots[2].material, std::string("PETG"));

  CHECK(hh.slots[0].prepped && hh.slots[0].ready && hh.slots[0].tool_loaded);
  CHECK(!hh.slots[1].prepped && !hh.slots[1].ready);
  CHECK(hh.slots[2].prepped && !hh.slots[2].ready);   // GATE_UNKNOWN: present, unconfirmed
  CHECK(hh.slots[3].prepped && hh.slots[3].ready);
  CHECK_EQ(hh.loaded_slot, 0);

  // endless spool groups pair off into per-slot backups
  CHECK_EQ(hh.slots[0].backup, 1);
  CHECK_EQ(hh.slots[1].backup, 0);
  CHECK_EQ(hh.slots[2].backup, 3);
  CHECK(!hh.spoolman);
  CHECK(hh.slots[0].can_configure);
  CHECK(!hh.error && hh.message.empty() && !hh.bypass);

  // endless spool off: groups are stored but inert, so no backups
  mmu["endless_spool_enabled"] = false;
  load_state(j);
  hh.refresh();
  CHECK_EQ(hh.slots[0].backup, -1);

  // no rgb tuple published: fall back to a hex gate_color
  mmu.erase("gate_color_rgb");
  load_state(j);
  hh.refresh();
  CHECK_EQ(hh.slots[0].colour, std::string("ff0000"));
  CHECK_EQ(hh.slots[2].colour, std::string(""));       // a bare name cannot be resolved

  // spoolman pull: the database owns the gate map, so edits are refused
  mmu["spoolman_support"] = "pull";
  load_state(j);
  hh.refresh();
  CHECK(hh.spoolman);
  CHECK(!hh.slots[0].can_configure);
  mmu["spoolman_support"] = "push";
  load_state(j);
  hh.refresh();
  CHECK(hh.spoolman && hh.slots[0].can_configure);
}

static void test_hh_activity(KWebSocketClient &ws) {
  struct Case { const char *action; MmuActivity activity; };
  const Case cases[] = {
    {"Idle", MmuActivity::Idle},
    {"Loading", MmuActivity::Loading},
    {"Loading Ext", MmuActivity::Loading},
    {"Unloading", MmuActivity::Unloading},
    {"Exiting Ext", MmuActivity::Unloading},
    {"Forming Tip", MmuActivity::Moving},
    {"Heating", MmuActivity::Moving},
    {"Selecting", MmuActivity::Moving},
    {"Unknown", MmuActivity::Moving},
  };
  for (const Case &c : cases) {
    json j = hh_status();
    j["printer_state"]["mmu"]["action"] = c.action;
    load_state(j);
    HhBackend hh(ws);
    hh.refresh();
    CHECK(hh.activity == c.activity);
  }

  // a toolchange in flight: `gate` already names the destination, so the
  // incoming gate must not read as loaded
  json j = hh_status();
  json &mmu = j["printer_state"]["mmu"];
  mmu["next_tool"] = 2;
  mmu["gate"] = 2;
  mmu["filament"] = "Loaded";
  mmu["action"] = "Unloading";
  load_state(j);
  HhBackend hh(ws);
  hh.refresh();
  CHECK(hh.activity == MmuActivity::Swapping);
  CHECK_EQ(hh.loaded_slot, -1);
  CHECK(!hh.slots[2].tool_loaded);
}

static void test_hh_faults(KWebSocketClient &ws) {
  json j = hh_status();
  json &mmu = j["printer_state"]["mmu"];

  // an MMU fault pauses the print and says why; RESUME is the way out
  for (const char *state : {"paused", "pause_locked"}) {
    mmu["print_state"] = state;
    mmu["reason_for_pause"] = "Filament stuck in gate 1";
    load_state(j);
    HhBackend hh(ws);
    hh.refresh();
    CHECK(hh.error && hh.message_error);
    CHECK(hh.activity == MmuActivity::Error);
    CHECK_EQ(hh.message, std::string("Filament stuck in gate 1"));
    CHECK(!hh.can_load(0) && !hh.can_unload() && !hh.can_eject(0));
    CHECK(hh.can_set_backup(0));
    sent.clear();
    hh.reset_failure();
    CHECK_EQ(sent.size(), (size_t)1);
    CHECK_EQ(sent[0], std::string("RESUME"));
  }

  // klipper's own job error is not an MMU fault: HH has nothing to reset
  mmu["print_state"] = "error";
  mmu["reason_for_pause"] = "";
  load_state(j);
  {
    HhBackend hh(ws);
    hh.refresh();
    CHECK(!hh.error);
    CHECK(hh.message.empty());
    CHECK(hh.can_load(0));
  }

  // HH drives its own toolchanges during a print
  mmu["print_state"] = "printing";
  load_state(j);
  {
    HhBackend hh(ws);
    hh.refresh();
    CHECK(!hh.error);
    CHECK(!hh.can_load(0) && !hh.can_eject(0));
    CHECK(hh.can_set_backup(0));
  }

  // MMU ENABLE=0 refuses everything; say so without calling it a fault
  mmu["print_state"] = "ready";
  mmu["enabled"] = false;
  load_state(j);
  {
    HhBackend hh(ws);
    hh.refresh();
    CHECK(!hh.error && !hh.message_error);
    CHECK_EQ(hh.message, std::string("MMU disabled"));
    CHECK(!hh.can_load(0));
  }

  // bypass with filament in the extruder: no gate may load until it is out
  mmu["enabled"] = true;
  mmu["tool"] = -2;
  mmu["gate"] = -2;
  mmu["filament"] = "Loaded";
  load_state(j);
  {
    HhBackend hh(ws);
    hh.refresh();
    CHECK(hh.bypass);
    CHECK_EQ(hh.loaded_slot, -1);
    CHECK(!hh.can_load(0));
    mmu["filament"] = "Unloaded";
    load_state(j);
    hh.refresh();
    CHECK(hh.bypass && hh.can_load(0));
  }
}

static void test_hh_verbs(KWebSocketClient &ws) {
  json j = hh_status();
  json &mmu = j["printer_state"]["mmu"];
  mmu["ttg_map"] = {0, 0, 2, 3};   // gate 1 has no tool
  load_state(j);
  HhBackend hh(ws);
  hh.refresh();

  CHECK(hh.can_load(0) && !hh.can_load(1) && hh.can_load(2));  // gate 1 is empty
  CHECK(!hh.can_unload());
  CHECK(hh.can_eject(2) && !hh.can_eject(1));
  CHECK(hh.can_clear_colour());

  // a mapped gate goes through the full toolchange
  sent.clear();
  hh.load(0);
  CHECK_EQ(sent.size(), (size_t)1);
  CHECK_EQ(sent[0], std::string("MMU_CHANGE_TOOL TOOL=0"));

  // an unmapped gate is selected by hand
  sent.clear();
  hh.load(1);
  CHECK_EQ(sent[0], std::string("MMU_UNLOAD\nMMU_SELECT GATE=1\nMMU_LOAD"));

  sent.clear();
  hh.unload();
  CHECK_EQ(sent[0], std::string("MMU_UNLOAD"));

  sent.clear();
  hh.eject(2);
  CHECK_EQ(sent[0], std::string("MMU_EJECT GATE=2"));

  // gate map edits carry the gate's temperature so HH does not reset it
  sent.clear();
  hh.set_colour(0, "1A2B3C");
  CHECK_EQ(sent[0], std::string("MMU_GATE_MAP GATE=0 COLOR=1A2B3C TEMP=210"));

  sent.clear();
  hh.set_colour(1, "");
  CHECK_EQ(sent[0], std::string("MMU_GATE_MAP GATE=1 COLOR= TEMP=200"));

  sent.clear();
  hh.set_material(2, "PLA Silk");
  CHECK_EQ(sent[0], std::string("MMU_GATE_MAP GATE=2 MATERIAL=\"PLA Silk\" TEMP=240"));

  sent.clear();
  hh.set_material(99, "PLA");
  CHECK_EQ(sent.size(), (size_t)0);

  mmu.erase("gate_temperature");
  load_state(j);
  sent.clear();
  hh.set_material(2, "PETG");
  CHECK_EQ(sent[0], std::string("MMU_GATE_MAP GATE=2 MATERIAL=PETG"));

  // backups become endless spool groups, and turning one on enables the feature
  sent.clear();
  hh.set_backup(0, 2);
  CHECK_EQ(sent[0], std::string("MMU_ENDLESS_SPOOL ENABLE=1 GROUPS=0,1,0,3"));

  // the next edit before klipper echoes the first builds on what was sent
  sent.clear();
  hh.set_backup(1, 3);
  CHECK_EQ(sent[0], std::string("MMU_ENDLESS_SPOOL ENABLE=1 GROUPS=0,1,0,1"));

  // clearing moves the slot to a fresh group and leaves its partners together
  sent.clear();
  hh.set_backup(0, -1);
  CHECK_EQ(sent[0], std::string("MMU_ENDLESS_SPOOL ENABLE=1 GROUPS=2,1,0,1"));

  // klipper echoes the groups: the pending set retires and state is read again
  mmu["endless_spool_groups"] = {2, 1, 0, 1};
  load_state(j);
  hh.refresh();
  CHECK_EQ(hh.slots[1].backup, 3);
  CHECK_EQ(hh.slots[2].backup, -1);
  sent.clear();
  hh.set_backup(2, 0);
  CHECK_EQ(sent[0], std::string("MMU_ENDLESS_SPOOL ENABLE=1 GROUPS=0,1,0,1"));
}

static void test_hh_hostile_status(KWebSocketClient &ws) {
  const char *fields[] = {"num_gates", "ttg_map", "gate_status", "gate_material",
                          "gate_color", "gate_color_rgb", "gate_spool_id",
                          "gate_temperature", "endless_spool_groups",
                          "endless_spool_enabled", "gate", "tool", "next_tool",
                          "filament", "action", "print_state", "reason_for_pause",
                          "spoolman_support", "enabled"};
  for (const char *field : fields) {
    for (int shape = 0; shape < 4; shape++) {
      json j = hh_status();
      json &mmu = j["printer_state"]["mmu"];
      switch (shape) {
        case 0: mmu[field] = 17; break;
        case 1: mmu[field] = "surprise"; break;
        case 2: mmu[field] = json::array({1, 2}); break;  // too short for 4 gates
        case 3: mmu.erase(field); break;
      }
      load_state(j);
      HhBackend hh(ws);
      try {
        hh.refresh();
        for (int s = -1; s < 5; s++) {
          (void)hh.can_load(s);
          (void)hh.can_eject(s);
          (void)hh.can_set_backup(s);
        }
        hh.set_backup(0, 2);
        hh.set_colour(0, "");
        hh.load(0);
      } catch (const std::exception &e) {
        std::cout << "FAIL: hh field " << field << " shape " << shape
                  << " threw: " << e.what() << std::endl;
        failures++;
      }
    }
  }
}
#endif // MMU_BACKEND_HH

int main() {
  KWebSocketClient ws(NULL);

  test_lane_mapping(ws);
  test_activity(ws);
  test_messages(ws);
  test_spoolman(ws);
  test_permissions(ws);
  test_verbs(ws);
  test_hostile_status(ws);
#ifdef MMU_BACKEND_HH
  test_hh_gate_mapping(ws);
  test_hh_activity(ws);
  test_hh_faults(ws);
  test_hh_verbs(ws);
  test_hh_hostile_status(ws);
#endif

  if (failures > 0) {
    std::cout << failures << " backend check(s) failed" << std::endl;
    return 1;
  }
  std::cout << "All backend tests passed successfully!" << std::endl;
  return 0;
}
