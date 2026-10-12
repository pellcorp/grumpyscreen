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
| `SIM_PRINTING` | `0` | `1` starts `quick_cube.gcode`, or name a file |
| `SIM_ARGS` | | Any `fake_moonraker.py` flag, see below |
| `SIM_WIFI` | `1` | `0` runs without the fake wpa_supplicant |
| `SIM_WIFI_ARGS` | | Any `fake_wpa_supplicant.py` flag |
| `SIM_PORT` | `7125` | Moonraker port |
| `SIM_BUILD` | | Build folder to run, e.g. `build/sdl` |
| `SIM_CFG_SRC` | `grumpyscreen.cfg` | Config the session config is made from |
| `SIM_KEEP` | `0` | `1` keeps other UIs running, for side-by-side ports |

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
| `SIM_RUNOUT [LANE=x]`, `SIM_FAULT [LANE=x]` | AFC runout and lane fault |
| `SIM_FAULT GATE=n`, `SIM_ERROR` | Happy Hare jammed gate and pause_locked error |

`python3 tools/sim_send.py --rpc server.info` sends any Moonraker method; `--listen 10` prints notifications.

`python3 tools/sim_smoke.py` checks the simulator end to end.
