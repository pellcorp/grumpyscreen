#ifndef __AFC_BACKEND_H__
#define __AFC_BACKEND_H__

#include "mmu_backend.h"
#include "websocket_client.h"

#include <string>
#include <vector>

class AfcBackend : public MmuBackend {
 public:
  AfcBackend(KWebSocketClient &ws) : ws(ws) {}

  const char *vendor() const override { return "AFC"; }
  const char *backup_label() const override { return "Infinite\nSpool"; }

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
  void dismiss_message() override;

  // AFC refuses filament motion while a print is running (its own toolchanges
  // drive it then) and while the unit is bypassed or in an error state.
  bool can_load(int slot) const override;
  bool can_unload() const override;
  bool can_eject(int slot) const override;
  bool can_set_backup(int slot) const override;
  // SET_COLOR stores "#" + whatever it is given, so an empty argument leaves
  // AFC holding a bare "#": not a colour, but non-empty, so AFC still counts
  // the lane as coloured (RFID stops overwriting it, and its Snapmaker path
  // parses the hex and raises). AFC has no clear-colour command to send
  // instead, so the panel is told not to offer one.
  bool can_clear_colour() const override { return false; }

 private:
  // MmuSlot::name is a display name; AFC's gcode needs the raw lane name, so
  // keep them side by side, indexed the same as slots
  const std::string &lane_id(int slot) const {
    static const std::string none;
    return (slot >= 0 && (size_t)slot < lane_ids.size()) ? lane_ids[slot] : none;
  }

  bool motion_ok() const { return !busy() && !printing && !bypass; }

  KWebSocketClient &ws;
  std::vector<std::string> lane_ids;
  bool printing = false;
};

#endif // __AFC_BACKEND_H__
