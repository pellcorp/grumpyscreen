# Testing Without a Printer

Two fakes stand in for the printer, so the screen can be developed and checked without one. Both need only Python 3.

| Part | File |
| --- | --- |
| Fake Moonraker + Klipper | `tools/fake_moonraker.py` (code in `tools/moonsim/`) |
| Fake wpa_supplicant | `tools/fake_wpa_supplicant.py` |

## With a local build

```
./build.sh sdl --small
./sim.sh
```

`sim.sh` starts both fakes and the first of `build/sdl-small`, `build/sdl`, `build` that exists, and stops them when the screen exits.

Each run gets a fresh session in `/tmp/grumpy-sim-<port>`: a copy of the test prints, the UI config written for the session, saved UI settings, and the logs (`moonraker.log`, `wpa.log`). Deleting files, factory reset, shutdown, updates and wifi changes only ever touch that folder. The `[commands]` in the session config run `tools/sim_hook.py` instead of the printer's scripts.

## On their own

Any screen can use the fake printer, including one on a printer or another machine:

```
python3 tools/fake_moonraker.py --host 0.0.0.0
```

Point the screen's `[moonraker]` `host` and `port` at it, and set `thumbnail_path` so thumbnails are downloaded. Only `sim.sh` swaps the screen's `[commands]` for stand-ins; elsewhere they run for real. `fake_wpa_supplicant.py` is a local socket, so it only works on the machine running the screen (`WPA_SUPPLICANT_SOCKET=<dir>`).

## Settings

| Variable | Default | |
| --- | --- | --- |
| `SIM_BACKEND` | `afc` | `afc`, `hh` (Happy Hare) or `none` |
| `SIM_LANES` | `4` | Lanes or gates |
| `SIM_FANS` | `1` | How many of `grumpyscreen.cfg`'s fans exist (1-7) |
| `SIM_LEDS` | `1` | How many of its output_pin LEDs exist (0-5) |
| `SIM_SPOOLMAN` | `push` | `off`, `readonly`, `push` or `pull` |
| `HIDE_SPOOLMAN` | Config value | `true` hides the global Spoolman nav tab; `false` shows it. Overrides the config; MMU Spoolman integration stays available. |
| `SIM_PRINTING` | `0` | `1` starts `quick_cube.gcode`, or name a file |
| `SIM_ARGS` | | Any `fake_moonraker.py` flag, see below |
| `SIM_WIFI` | `1` | `0` runs without the fake wpa_supplicant |
| `SIM_WIFI_ARGS` | | Any `fake_wpa_supplicant.py` flag |
| `SIM_PORT` | `7125` | Moonraker port |
| `SIM_BUILD` | | Build folder to run, e.g. `build/sdl` |
| `SIM_CFG_SRC` | `grumpyscreen.cfg` | Config the session config is made from |
| `SIM_KEEP` | `0` | `1` keeps other UIs running, for side-by-side ports |

Run `HIDE_SPOOLMAN=true ./sim.sh` to test with the global Spoolman tab hidden. The same environment variable works when launching GrumpyScreen directly. Unset or invalid values follow the config.

Useful `SIM_ARGS` flags (`python3 tools/fake_moonraker.py --help` lists all):

| Flag | |
| --- | --- |
| `--extruders N` | Up to 4 extruders; with `SIM_BACKEND=none`, `T<n>` activates one |
| `--rgb-led`, `--heater-fans`, `--chamber-heater` | Extra hardware, added to the session config too |
| `--speed X` | How much faster than real time prints run (20) |
| `--update MODE` | COSMOS update check: `none`, `ok`, `fail`, `refuse`, `nightly`, `invalid`, `busy`, `missing`; a comma-separated list moves on each refresh |
| `--klippy STATE` | Start `ready`, `shutdown` or `disconnected` |
| `--afc-multi-map` | AFC `enable_multiple_mapping` |
| `--message TEXT`, `--message-type error` | Seed the AFC message queue |
| `--spoolman-lanes 1,3` | Give AFC lanes a Spoolman spool |
| `--http-thumbnails` | The UI fetches thumbnails over HTTP |
| `--history N`, `--queue a,b` | Seed print history and the job queue |

`fake_wpa_supplicant.py` starts joined to `HomeNet`; every secured network's password is `printer123`. `--fail wrong-key|assoc-reject|not-found` makes connects fail, `--known "SSID:psk"` seeds a saved network.

## Test prints

`tools/sim_gcodes/` holds real Klipper gcode in OrcaSlicer's layout, with a short start and end sequence, embedded thumbnails and object labels. `tools/make_sim_gcodes.py` regenerates them.

| File | For |
| --- | --- |
| `quick_cube.gcode` | A quick single-colour print |
| `plate_5_parts.gcode` | Exclude object; the bracket's centre lies outside its outline |
| `keychain_4colour.gcode` | Four colours, plus a fifth tool the file never uses |
| `plate_6colour.gcode` | Six tools, two the same colour, mixed materials |
| `stripes_2colour.gcode` | Frequent tool changes |
| `pause_at_layer_6.gcode` | Pauses itself, like a filament change |
| `error_out_of_range.gcode` | Ends in Klipper's error state |
| `no_object_labels.gcode`, `no_thumbnail.gcode` | Empty states |
| `calibration/`, `spare_parts/` | Subfolders, a temperature tower and a long file name |

## Commands

From the console panel or `python3 tools/sim_send.py "SIM_PRINT"`:

| Gcode | |
| --- | --- |
| `SIM_PRINT [FILE=x]` | Start a print, or cancel the running one |
| `SIM_SHUTDOWN`, `SIM_READY` | Klipper shuts down / restarts |
| `SIM_PROMPT` | Show an `action:prompt` dialog |
| `SIM_UPLOAD [FILE=x]` | A new file arrives |
| `SIM_DISCONNECT [SECONDS=5]` | Moonraker drops every client |
| `SIM_KLIPPER_STOP [SECONDS=10]` | The Klipper service stops, then starts |
| `SIM_MESSAGE TEXT=.. [TYPE=error]` | AFC message |
| `PREP` | AFC lane checks and coloured Box Turtle artwork, using the simulated sensors |
| `SIM_AFC_CONSOLE` | AFC PREP plus warning HTML and a Unicode debug timing sample; leaves the message queue alone |
| `SIM_HH_CONSOLE [SAMPLE=all\|boot\|thin\|sensors]` | Replay captured Happy Hare HTML, alignment spaces and Unicode diagrams |
| `SIM_RUNOUT [LANE=x]`, `SIM_FAULT [LANE=x]` | AFC runout and lane fault |
| `SIM_FAULT GATE=n`, `SIM_ERROR` | Happy Hare jammed gate and pause_locked error |

`python3 tools/sim_send.py --rpc server.info` sends any Moonraker method; `--listen 10` prints notifications.

`python3 tools/sim_smoke.py` checks the simulator end to end.

### AFC console formatting (#442)

Run `python3 tools/sim_send.py "SIM_AFC_CONSOLE"` with the console open to
reproduce AFC's formatting. `PREP` reports the current sensors without moving
motors. With `SIM_LANES=5`, the staged fifth lane produces `NOT LOADED` and
the error artwork; the default four lanes produce the ready artwork.

These samples follow the sibling AFC plugin's `AFC_prep`, `AFC_BoxTurtle`,
`AFC_extruder`, `AFC_logger` and `afcDeltaTime` output. The mock uses Box Turtle
artwork labelled with its existing `CANVAS_1` unit. AFC sends raw messages with
unquoted `<span class=success--text>` (also `error`, `warning`, `info`, `primary`
and `secondary`), adjacent spans, embedded newlines and repeated literal spaces.
The ready artwork even leaves its final success span open upstream; the mock
preserves that. These logger messages have no `// ` prefix and aren't split
into separate responses. Warnings retain HTML, while errors use `!! `.

No tab-based alignment or HTML character entities were found in these AFC
paths. Its debug timings contain the literal Unicode character `Δ`; the sample
uses fixed example durations. The screenshot in
[#442](https://github.com/pellcorp/grumpyscreen/issues/442) shows Happy Hare's
inline `style="color:..."`, `<b>` tags and Unicode diagrams, which differ from
these AFC samples. These commands supply console input for investigating the
renderer.

The console translates `<span style="color:#RRGGBB">` (also `#RGB`) into
inline foreground colours. AFC's `success--text`, `warning--text` and
`error--text` classes use the same theme colours as echo, warning and error
lines; `primary--text`, `secondary--text` and `info--text` use their theme
colours. Nested spans restore the surrounding colour when closed. Unknown
span colours inherit the surrounding colour. `<b>` tags are removed while
retaining their text. Turning console colours off still removes these tags.
Sent commands remain literal. Unicode symbols still depend on font coverage.

### Happy Hare console formatting (#442)

Start `SIM_BACKEND=hh ./sim.sh`, open the console, then run:

```
python3 tools/sim_send.py "SIM_HH_CONSOLE"
python3 tools/sim_send.py "SIM_HH_CONSOLE SAMPLE=boot"
```

`boot` reproduces the screenshot's four-gate map, bypass column, black/magenta
swatches, selected-gate triangle and bold UNLOADED filament diagram. `thin`
shows a loaded filament using thin lines and an arrow tip. `sensors` includes
triggered/clear sensor circles, the encoder marker, loading arrows and a
coloured warning. The default `all` replays all three groups. These are fixed
formatting examples, independent of the simulator's current gate count or
filament state; replaying them does not change that state.

The captures use the sibling checkout's actual `_mmu_visual_to_string`,
`_get_filament_char`, `get_filament_position_string` and
`MmuLogger._color_message` methods. They retain adjacent inline colour spans,
nested `<b>`/`<span>` tags, literal Unicode glyphs and non-breaking spaces
(`U+00A0`), rather than HTML entities or tabs. Happy Hare uses Klipper's
`respond_info`, so each line arrives prefixed with `// `.

`tools/moonsim/hh_console_samples.json` records the source commit. Regenerate
it with `python3 tools/make_hh_console_samples.py ../Happy-Hare`; the generator
executes the formatting methods against plain data using Happy Hare's own
filament-display test adapter, without booting Klipper. The simulator itself
needs only the checked-in captures, with no sibling checkout or extra Python
dependencies. Check replay over the websocket and in console history with
`python3 tools/sim_smoke.py --hh-console-only`.
