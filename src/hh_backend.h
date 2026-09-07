#ifndef __HH_BACKEND_H__
#define __HH_BACKEND_H__

#include "mmu_backend.h"
#include "websocket_client.h"

#include <map>

// Happy Hare driver: reads the single "mmu" printer object, speaks MMU_*.
class HhBackend : public MmuBackend {
 public:
  HhBackend(KWebSocketClient &ws) : ws(ws) {}

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
  void reset_failure() override;

  // Happy Hare refuses filament motion mid-print (its own toolchanges drive it
  // then) and while it is paused waiting to be recovered.
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
  // busy() already covers the paused states: they report Error
  bool motion_ok() const { return enabled && !busy() && !in_print; }

  KWebSocketClient &ws;

  // HH publishes gate_spool_id but never grams; weights come from spoolman
  std::map<int, int> spool_weights;      // spool id -> grams remaining
  std::vector<int> fetched_spool_ids;    // ids covered by the last fetch

  // endless spool groups sent but not yet echoed back by klipper; bridges
  // the panel's back-to-back clear-then-set command pairs
  std::vector<int> pending_groups;

  bool enabled = true;          // MMU ENABLE=0 refuses every command
  bool filament_loaded = false; // in the extruder, from a gate or the bypass
  // from print_state: HH's own job state machine, not klipper's
  bool in_print = false;  // started | printing
  bool paused = false;    // paused | pause_locked, cleared by RESUME
};

#endif // __HH_BACKEND_H__
