# MmuBackend API

`MmuPanel` renders filament slots and drives them through `MmuBackend`
(`src/mmu_backend.h`). It holds no vendor knowledge. Supporting a new
multi-material unit means implementing this interface; the panel is not touched.

Reference implementation: `src/afc_backend.{h,cpp}` (AFC).

---

## Implement

Ten methods are pure virtual.

### Identity and detection

| Method | Called | Must do |
|---|---|---|
| `const char *vendor() const` | on selection, for the log line | return a display name |
| `bool detect()` | when klipper starts, and again on **every** reconnect | true if this vendor's objects are in `/printer_objs/objects`. No status data exists yet |
| `bool owns_update(json &j)` | on **every** websocket notification | true if `j` contains this vendor's objects. Keep cheap — returning false skips the redraw |
| `void refresh()` | before every redraw, UI lock held | rebuild **Fill** (below) from `State` |

### Verbs

Fire and forget: send gcode and return. Do not write to `slots` — real state
arrives via the subscription. `slot` is an index into `slots`.

| Method | Contract |
|---|---|
| `void load(int slot)` | ends with that slot loaded to the tool. Whether that is a plain load or a swap out of whatever is loaded now is yours to decide — the panel asks for the end state only |
| `void unload()` | filament leaves the tool, **stays in its slot** — slot keeps `prepped`/`ready` |
| `void eject(int slot)` | filament leaves the unit — slot becomes empty |
| `void set_colour(int slot, const std::string &hex)` | `"RRGGBB"`, no `#`. `""` clears, and is only ever sent when `can_clear_colour()` |
| `void set_material(int slot, const std::string &material)` | |
| `void set_backup(int slot, int backup)` | `backup` is a slot index, `-1` clears |

The panel closes the edit screen on `load` and `unload` and waits for
`loaded_slot` to follow, so a backend that only moved a selector here would
leave it showing stale state.

Slot indices are the panel's currency, but vendor gcode wants the vendor's own
name for the slot, and `MmuSlot::name` is a display name you may have prettied
up. Keep the raw ids in a vector indexed the same as `slots` and look them up
in the verbs — `AfcBackend::lane_id()`.

### Permissions

Five have defaults you can leave alone; `can_*` is where your own rules live.

| Method | Default | Override to say |
|---|---|---|
| `bool can_load(int slot) const` | slot is `ready`, not already loaded, unit not busy | ... and not mid-print, not in bypass, whatever else the vendor refuses |
| `bool can_unload() const` | something is loaded, unit not busy | |
| `bool can_eject(int slot) const` | slot has filament, unit not busy | |
| `bool can_set_backup(int slot) const` | more than one slot | |
| `bool can_clear_colour() const` | true | false if `set_colour(slot, "")` would not really clear it; the panel then drops its clear control |

The panel greys a control when the answer is false and never second-guesses a
true, so this is the one place a verb's rules belong. A verb the vendor does not
have at all returns false always and its control stays greyed for good — there
is no "supports X" flag to set.

`busy()` is `activity != Idle`, which includes `Error`: a unit that needs a
reset is not a unit that should be taking motion commands.

Two gates stay in the panel and are not yours to relax: unload is offered on
the loaded slot only, and a slot must be unloaded before it can be ejected.
Those protect the nozzle rather than model a vendor.

---

## Fill

Set by `refresh()`, read by the panel. Clear what you cannot determine rather
than leaving stale values.

### `slots` — `std::vector<MmuSlot>`

| Field | Type | Meaning |
|---|---|---|
| `name` | `string` | display name, unique in the unit (`Lane 1`, `Gate 0`) |
| `map` | `string` | tool label(s), `T0` or `T0,T1`. `""` = unmapped |
| `material` | `string` | `""` = unset |
| `colour` | `string` | `RRGGBB` no `#`. `""` = unset, draws the empty mark |
| `backup` | `int` | slot **index** taking over on runout, `-1` = none |
| `weight` | `int` | grams remaining, `0` = unknown |
| `prepped` | `bool` | filament physically present at the slot |
| `ready` | `bool` | fed into the unit, loadable |
| `tool_loaded` | `bool` | in the toolhead |
| `can_configure` | `bool` | may colour/material be edited here. Default true; set false only when something else owns the metadata |

`prepped` and `ready` are not the same thing and the panel does not conflate
them: a slot with filament touching a prep sensor renders as full and can be
ejected, but `can_load` defaults to `ready` because that is what a vendor's
load actually needs.

### Scalars on `MmuBackend`

| Field | Type | Meaning |
|---|---|---|
| `loaded_slot` | `int` | index loaded to the tool, `-1` = none |
| `activity` | `MmuActivity` | `Idle`, `Loading`, `Unloading`, `Swapping`, `Ejecting`, `Moving`, `Error` — map your own status onto the nearest, and report `Moving` for anything else that moves. The panel owns the wording it shows |
| `error` | `bool` | unit stopped. Greys the verbs (via `busy()`), draws nothing |
| `bypass` | `bool` | unit bypassed, single spool |
| `spoolman` | `bool` | `weight` is meaningful |
| `prompt` | `MmuPrompt` | a question for the user, `id == ""` = none. See below |

### Errors and prompts

The panel draws no banner and no fault icon. A stopped unit greys its verbs,
and the rest of the story is told the way klipper already tells it: the
console panel shows every line the vendor writes there, and the prompt panel
shows any `action:prompt` dialog a vendor macro raises, no MMU code involved.

`prompt` is for the vendor that stops and asks nothing. Fill it in `refresh()`
when the vendor is in a state the user has to act on and the vendor puts up no
dialog of its own. The panel renders it as the standard popout — title
banner, message, one row of keys — over whatever screen is showing. A tap sends
that key's gcode and closes the box.

| Field | Meaning |
|---|---|
| `id` | names the situation. The panel shows each id **once**: fill the same prompt on every refresh and the box is not re-raised, and a user who tapped it away is not nagged. A different id replaces the box, `""` takes it down |
| `title`, `text` | banner and body. Use the vendor's own words for the body — the line it sent to the console |
| `buttons` | `{label, gcode, danger}`. Every button sends gcode and closes; there is no callback into the backend, and no button is a plain "close" — tapping the dim overlay is not a thing either, so give the user a real way out. `danger` paints the key in the danger colour (cancel, abort) |

Make `id` carry the reason text (`"error:" + text`), so a new fault after a
tapped-away one gets a fresh box.

**Do not raise one for a vendor that already prompts.** Happy Hare's
`_MMU_ERROR_DIALOG` macro raises a klipper prompt on every in-print fault and
the prompt panel shows it; a second box from here for the same fault is worse
than none. So `HhBackend` fills nothing for `pause_locked`. `AfcBackend` does
fill one on `error_state`: AFC writes a console line but raises no dialog
(its prompt helper serves calibration and test flows only), so the backend
offers `RESET_FAILURE` (AFC's own recovery) and, while the print is paused,
`RESUME`.

**A fault that is only a console line is still a fault.** Standalone, Happy
Hare's `handle_mmu_error()` writes `!! MMU issue: <reason>` and changes no
published state, so a failed load from the panel would otherwise look like
nothing happened. `HhBackend` subscribes to `notify_gcode_response`, keeps
that one line, and fills a prompt with it — an `OK` key with no gcode, since
there is no state to reset. Event-driven prompts follow the same rule as any
other event: store what arrived, call `changed()`, and let `refresh()` fill
`prompt` from it. Put a counter in the id so the same text twice is two
prompts, and clear it when the vendor moves again.

Recovery gcode is the vendor's, never a panel verb: there is no
`reset_failure()` in the interface, because both vendors route it through the
printer's own RESUME and the print panel's Resume button already does that.

---

## Call

### Read printer state

```cpp
State *state = State::get_instance();
json &obj = state->get_data("/printer_state/mmu"_json_pointer);
json &objs = state->get_data("/printer_objs/objects"_json_pointer);
```

`/printer_objs/objects` is the object-name list (use in `detect()`);
`/printer_state/<object>` is live status. Type-check every field you read —
`is_string()`, `is_boolean()`, `is_array()` — never just `is_null()`. Fields
come and go across vendor versions and change type between them, and `refresh()`
runs on the websocket thread: a `json` type error there takes grumpyscreen down.

### Send

```cpp
ws.gcode_script("MMU_UNLOAD");                       // fire and forget
ws.send_jsonrpc(method, params, [this](json &d){});  // moonraker RPC + callback
```

`KWebSocketClient &ws` is yours to hold; the panel passes it to your constructor.

Klipper splits extended gcode parameters with `shlex`, so any value that could
contain whitespace, `#` or `;` has to be quoted before it goes into a command —
see `quote_value` in `afc_backend.cpp`.

### Request a redraw

```cpp
std::function<void()> changed;   // set by the panel, may be null
```

Call `changed()` when state arrives outside a `refresh()` — an async RPC
response, typically. It takes the UI lock, so **never call it synchronously from
inside `refresh()`** (which already runs under that lock) or you deadlock. Guard
against re-entry: the `refresh()` it triggers must not fire the same request
again.

---

## Register

In `GuppyScreen::GuppyScreen()` (`src/guppyscreen.cpp`), as a member so it
outlives the panel:

```cpp
mmu_panel.add_backend("afc", &afc_backend);   // id = the /mmu/backend value
```

The backend is chosen in `grumpyscreen.cfg`, never probed for:

```ini
[mmu]
backend: afc
```

`none` (the default) means no tab, no probing and no LVGL allocation. For any
other value the tab is present from startup but its button is disabled;
`MmuPanel::select_backend()` runs `detect()` on that one backend when klipper
starts and the button is enabled if it passes, otherwise it logs and carries on.

---

## Notes

Only shared concepts belong in `MmuBackend`. A feature one vendor has and
another lacks stays inside the implementation.

The panel is a flat list of slots. A vendor with several physical units reports
their slots one after another and names them so they read unambiguously
(`Gate 0`..`Gate 11`, `lane1`..`lane8`); there is no unit grouping, and a
bypass path is not a slot — report it with the `bypass` flag instead.

Where the two current vendors differ is instructive:

| | AFC | Happy Hare |
|---|---|---|
| slot | lane (`AFC_lane lane1` object per lane) | gate (index into the one `mmu` object's arrays) |
| loadable | `load` (the lane's own switch, or `loaded_to_hub` on a virtual hub) | `gate_status` not `GATE_EMPTY` |
| tool map | per-lane `map` list of `T(n)` macros | `ttg_map`, indexed by tool |
| load verb | `TOOL_LOAD` fresh, `CHANGE_TOOL` to swap | `MMU_CHANGE_TOOL` either way; unmapped gates need `MMU_SELECT` + `MMU_LOAD` |
| backup | a runout pointer per lane (`SET_RUNOUT`) | endless-spool groups, a set per gate |
| busy | `current_state` enum | `action` string, plus a pending `next_tool` for a swap |
| fault | `error_state`, message queue for the text, no dialog — `prompt` filled, `RESET_FAILURE`/`RESUME` clear | `print_state` `pause_locked` + its own `_MMU_ERROR_DIALOG` klipper prompt — no `prompt` filled, `RESUME` clears |
| clear a colour | impossible, `can_clear_colour()` false | empty `COLOR=` is accepted |
| metadata owner | spoolman owns a lane with a `spool_id`, so `can_configure` false there | spoolman owns the whole gate map in `pull` mode only |

None of that reaches the panel.

Report what the vendor reports; do not normalise. Whether ejecting clears the
spool colour is a per-vendor choice — AFC has a `remember_spool` option for it.
The panel mirrors metadata and only writes back what the user changed.

`can_configure` is not about filament presence — AFC accepts metadata on an
empty slot and pre-staging one is a real workflow. Set it false where the
vendor hands metadata ownership elsewhere: `AfcBackend` does it for a lane with
a spoolman spool assigned, because AFC refetches that lane's colour and
material at every PREP and a local edit would apply and then revert. The panel
greys the editing controls and marks the slot locked.

`can_clear_colour()` exists for the same reason in reverse: AFC's `SET_COLOR`
stores `"#"` for an empty argument, which is not a colour but is not empty
either, so AFC keeps treating the lane as coloured. A vendor that cannot
express "no colour" says so and the panel stops offering it.
