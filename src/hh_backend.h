#ifndef __HH_BACKEND_H__
#define __HH_BACKEND_H__

#include "mmu_backend.h"
#include "websocket_client.h"

#include <chrono>
#include <map>
#include <mutex>

// Happy Hare driver: reads the single "mmu" printer object, speaks MMU_*.
//
// Errors. In a print HH pauses (print_state pause_locked) and its own
// _MMU_ERROR_DIALOG macro raises a klipper prompt, which the prompt panel
// shows -- nothing to add here. Standalone, HH changes no published state at
// all: handle_mmu_error() writes one console line, "!! MMU issue: <reason>",
// and returns. That line is the whole report, so it is picked up from the
// gcode responses and offered as the panel's prompt; otherwise a failed load
// from the panel would look like nothing happened.
class HhBackend : public MmuBackend {
 public:
  HhBackend(KWebSocketClient &ws);

  // notify_gcode_response; public for the tests, the websocket client calls it
  void handle_gcode_response(json &j);

  const char *vendor() const override { return "Happy Hare"; }

  bool detect() override;
  bool owns_update(json &j) override;
  void refresh() override;

  void load(int slot) override;
  void unload() override;
  void eject(int slot) override;
  void set_colour(int slot, const std::string &hex) override;
  void set_material(int slot, const std::string &material) override;
  void set_backup(int slot, int backup) override;

  // Happy Hare refuses filament motion mid-print (its own toolchanges drive it
  // then) and while a fault has it locked (pause_locked, until RESUME).
  bool can_load(int slot) const override;
  bool can_unload() const override;
  bool can_eject(int slot) const override;
  bool can_set_backup(int slot) const override;
  // can_clear_colour() is left at true: MMU_GATE_MAP's validate_color accepts
  // an empty string and stores it, so "no colour" is a colour Happy Hare has.

 private:
  void fetch_spoolman_weights();
  void gate_map(int slot, const std::string &args);
  void send_groups(const std::vector<int> &groups);
  std::vector<int> current_groups() const;
  // busy() already covers pause_locked: it reports Error
  bool motion_ok() const { return enabled && !busy() && !in_print; }

  KWebSocketClient &ws;

  // HH publishes gate_spool_id but never grams; weights come from spoolman
  std::map<int, int> spool_weights;      // spool id -> grams remaining
  std::vector<int> fetched_spool_ids;    // ids covered by the last fetch
  std::chrono::steady_clock::time_point last_fetch;
  bool fetch_failed = false;             // last fetch got nothing back

  bool enabled = true;          // MMU ENABLE=0 refuses every command
  bool filament_loaded = false; // in the extruder, from a gate or the bypass
  // from print_state: HH's own job state machine, not klipper's
  bool in_print = false;  // started | printing

  // the last standalone "MMU issue" line, waiting to become the prompt. Set on
  // the websocket thread, read by refresh() under the UI lock, hence the
  // mutex; the count keeps a repeat of the same text a new prompt. Cleared
  // once HH is doing something again.
  std::mutex issue_lock;
  std::string issue;
  int issue_count = 0;
  bool was_moving = false;  // last refresh's action != Idle
};

#endif // __HH_BACKEND_H__
