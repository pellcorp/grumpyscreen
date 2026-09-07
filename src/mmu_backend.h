#ifndef __MMU_BACKEND_H__
#define __MMU_BACKEND_H__

#include "hv/json.hpp"

#include <functional>
#include <string>
#include <vector>

using json = nlohmann::json;

// One filament slot (a lane on AFC, a gate elsewhere) in neutral terms.
struct MmuSlot {
  std::string name;      // backend's display name, unique within the unit
  std::string map;       // tool label(s), e.g. "T0" or "T0,T1" ("" = unmapped)
  std::string material;
  std::string colour;    // "RRGGBB", "" when unset
  int backup = -1;       // slot index that takes over when this one runs out
  int weight = 0;        // grams remaining (spoolman), 0 = unknown
  bool prepped = false;  // filament physically present at the slot
  bool ready = false;    // fed into the unit, ready to load
  bool tool_loaded = false;
  // whether colour/material may be edited here. AFC accepts edits on an empty
  // slot, so this is not about filament presence — it is for backends that hand
  // metadata ownership elsewhere, e.g. pulling it from spoolman instead.
  // Backends that never refuse leave it true.
  bool can_configure = true;
};

// What the unit is doing, in terms every unit shares. The panel owns the
// wording it shows; a backend maps its own status onto the nearest of these
// and reports Moving for anything else that involves motion.
enum class MmuActivity {
  Idle,       // resting, ready to be told to do something
  Loading,    // feeding a slot to the tool
  Unloading,  // pulling filament out of the tool
  Swapping,   // exchanging one slot for another
  Ejecting,   // backing filament out of the unit
  Moving,     // moving for some other reason (docking, calibrating, restoring)
  Error,      // stopped, needs reset_failure() before anything else
};

// The MMU panel renders slots and calls these verbs; it never knows which
// vendor answers. Only concepts every supported backend shares belong here —
// anything vendor-specific stays inside an implementation.
//
// Writing a backend: docs/mmu-backends.md
class MmuBackend {
 public:
  virtual ~MmuBackend() {}

  virtual const char *vendor() const = 0;

  // this backend's objects are present in the current klipper config
  virtual bool detect() = 0;
  // this status delta belongs to this backend
  virtual bool owns_update(json &j) = 0;
  // Rebuild the neutral state below from State; called before the panel
  // redraws, always with the UI lock already held. Do not call changed() from
  // here -- see its note below.
  virtual void refresh() = 0;

  // Verbs; slot arguments index into slots. The panel closes the edit screen
  // on load and unload and waits for loaded_slot to follow, so a backend that
  // only moved a selector here would leave the panel showing stale state.
  virtual void load(int slot) = 0;  // end with this slot in the tool, whatever
                                    // is loaded now (a fresh load or a swap --
                                    // the backend decides which)
  virtual void unload() = 0;        // unload whatever is in the tool
  virtual void eject(int slot) = 0; // back out of the unit entirely
  virtual void set_colour(int slot, const std::string &hex) = 0;  // "" clears
  virtual void set_material(int slot, const std::string &material) = 0;
  virtual void set_backup(int slot, int backup) = 0;              // -1 clears
  virtual void reset_failure() = 0;

  // May the panel offer a verb right now? It greys the control when the answer
  // is false and never second-guesses a true, so this is where a vendor's own
  // rules live -- refusing during a print, in bypass, on an unfed slot. The
  // defaults below are the conservative reading of the neutral state; override
  // whenever the vendor knows better.
  //
  // A verb that is never available (a unit that cannot eject, say) returns
  // false always and the panel hides that control -- there is no need for
  // "does this vendor support X" flags.
  virtual bool can_load(int slot) const {
    return !busy() && valid(slot) && slots[slot].ready && !slots[slot].tool_loaded;
  }
  virtual bool can_unload() const { return !busy() && loaded_slot >= 0; }
  virtual bool can_eject(int slot) const {
    return !busy() && valid(slot) && (slots[slot].prepped || slots[slot].ready);
  }
  // backup/infinite-spool wiring: configuration, not motion, so this is not
  // gated on the unit resting by default
  virtual bool can_set_backup(int slot) const { return valid(slot) && slots.size() > 1; }
  // whether set_colour(slot, "") really clears the colour. A vendor with no
  // command for it says false and the panel drops its clear control rather
  // than offering a tap that quietly stores something invalid.
  virtual bool can_clear_colour() const { return true; }

  // Acknowledge the current `message`. Optional: only backends that hold
  // messages in a queue of their own have anything to do here, and for them a
  // local dismissal is not enough -- an unacknowledged message sits at the head
  // of that queue and hides every later one. Backends whose message is derived
  // from live state have nothing to pop and leave this alone.
  virtual void dismiss_message() {}

  // mid-operation: filament is moving, or the unit is stopped needing a reset
  bool busy() const { return activity != MmuActivity::Idle; }

  // neutral state, valid after refresh()
  std::vector<MmuSlot> slots;
  int loaded_slot = -1;     // slot currently loaded to the tool
  MmuActivity activity = MmuActivity::Idle;
  std::string message;      // banner text ("" = none)
  bool message_error = false; // banner is a failure, not information
  bool error = false;       // unit stopped; tapping the banner resets it
  bool bypass = false;      // unit bypassed, printing from a single spool
  bool spoolman = false;    // weights are meaningful

  // Backend-initiated update, for state that arrives outside refresh() -- an
  // async RPC response, typically. Set by the panel; may be null.
  //
  // Call it only from the websocket thread with the UI lock NOT held, i.e.
  // from a response callback. It takes that lock itself, and refresh() and the
  // verbs already run under it, so calling it from any of those deadlocks the
  // UI. Guard against re-entry too: the refresh() it triggers must not fire
  // the same request again.
  std::function<void()> changed;

 protected:
  bool valid(int slot) const { return slot >= 0 && (size_t)slot < slots.size(); }
};

#endif // __MMU_BACKEND_H__
