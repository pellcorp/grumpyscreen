"""The simulated klipper: config, printer objects, gcode, motion, heaters, and
virtual_sdcard actually running the gcode files.

Behaviour and messages follow klippy's own sources (gcode.py, toolhead.py,
gcode_move.py, heaters.py, extruder.py, output_pin.py, fan*.py, led.py,
pause_resume.py, virtual_sdcard.py, print_stats.py, exclude_object.py), so the
UI meets the same answers it gets from a real printer.
"""
import asyncio
import json
import math
import random
import re
import time

from . import state as S, mmu
from .state import STATE, GcodeError, console, respond_info

ROOM = 24.0
CONFIG = {}            # section -> typed settings; the object names, case kept
MACROS = ["LOAD_FILAMENT", "UNLOAD_FILAMENT", "_GUPPYSCREEN_EXTRUDE", "_GUPPYSCREEN_RETRACT",
          "_CLIENT_LINEAR_MOVE"]

# grumpyscreen.cfg names these; --fans N / --leds N expose the first N
FAN_IDS = ["fan", "fan_generic part", "fan_generic chamber", "output_pin fan0",
           "output_pin fan1", "fan_generic auxiliary", "output_pin fan2"]
LED_IDS = ["output_pin LED", "output_pin red_pin", "output_pin green_pin",
           "output_pin yellow_pin", "output_pin light_pin"]

HOOKS = {}             # moonraker's side: print_event, klippy_state, sd_line
mark = S.mark


# ------------------------------------------------------------------ config
def extruder_names():
    return ["extruder"] + ["extruder%d" % i for i in range(1, S.ARGS.extruders)]


def build_config():
    """Only what the UI or the simulation reads."""
    a = S.ARGS
    CONFIG.clear()
    CONFIG["printer"] = {"kinematics": "corexy", "max_velocity": 500.0, "max_accel": 20000.0}
    for axis, lo in (("x", 0.0), ("y", 0.0), ("z", -2.0)):
        CONFIG["stepper_" + axis] = {"position_min": lo, "position_max": 256.0, "position_endstop": 0.0}
    for name in extruder_names():
        CONFIG[name] = {"nozzle_diameter": 0.4, "filament_diameter": 1.75, "min_temp": 0.0,
                        "max_temp": 300.0, "min_extrude_temp": 170.0, "max_extrude_only_distance": 100.0,
                        "pressure_advance": 0.035, "pressure_advance_smooth_time": 0.04}
    CONFIG["heater_bed"] = {"min_temp": 0.0, "max_temp": 120.0}
    if a.chamber_heater:
        CONFIG["heater_generic chamber_heater"] = {"min_temp": 0.0, "max_temp": 70.0}
    CONFIG["temperature_fan chamber_fan"] = {"min_temp": 0.0, "max_temp": 70.0}
    CONFIG["temperature_sensor chamber_temp"] = {"min_temp": 0.0, "max_temp": 100.0}
    for f in FAN_IDS[:max(1, min(a.fans, len(FAN_IDS)))]:
        # the UI drives output_pin fans 0-255, which klipper accepts with scale: 255
        CONFIG[f] = {"pwm": True, "scale": 255.0} if f.startswith("output_pin") else {}
    for led in LED_IDS[:max(0, min(a.leds, len(LED_IDS)))]:
        CONFIG[led] = {"pwm": False, "scale": 1.0}
    if a.rgb_led:
        CONFIG["led chamber_light"] = {}
    if a.heater_fans:
        CONFIG["heater_fan hotend_fan"] = {"heater": ["extruder"], "heater_temp": 50.0, "fan_speed": 1.0}
        CONFIG["controller_fan mcu_fan"] = {"heater": ["extruder", "heater_bed"], "idle_timeout": 30,
                                            "fan_speed": 1.0}
    CONFIG["virtual_sdcard"] = {"path": a.session_dir + "/gcodes", "on_error_gcode": "TURN_OFF_HEATERS"}
    CONFIG["idle_timeout"] = {"timeout": float(a.idle_timeout), "gcode": "TURN_OFF_HEATERS\nM84"}
    CONFIG["screws_tilt_adjust"] = {"screw_thread": "CW-M3", "speed": 50.0, "horizontal_move_z": 5.0}
    for i, (name, x, y) in enumerate(SCREWS, 1):
        CONFIG["screws_tilt_adjust"]["screw%d" % i] = [x, y]
        CONFIG["screws_tilt_adjust"]["screw%d_name" % i] = name


SCREWS = (("front left", 30.0, 30.0), ("front right", 226.0, 30.0),
          ("rear right", 226.0, 226.0), ("rear left", 30.0, 226.0))


def raw_config():
    """configfile.config: every value as the text it would be in printer.cfg."""
    def text(v):
        if isinstance(v, list):
            return ", ".join(text(x) for x in v)
        if isinstance(v, float) and v == int(v):
            return "%d" % v
        return str(v)
    return {sec: {k: text(v) for k, v in opts.items()} for sec, opts in CONFIG.items()}


def heaters():
    return extruder_names() + ["heater_bed"] + (
        ["heater_generic chamber_heater"] if "heater_generic chamber_heater" in CONFIG else [])


def build_objects(keep=()):
    """Fresh printer objects, as after a klipper (re)start. MMU objects survive."""
    kept = {k: STATE[k] for k in keep if k in STATE}
    STATE.clear()
    STATE.update(kept)
    ext = extruder_names()
    STATE["webhooks"] = {"state": S.KLIPPY["state"], "state_message": S.KLIPPY["message"]}
    STATE["configfile"] = {"config": raw_config(),
                           "settings": {k.lower(): dict(v) for k, v in CONFIG.items()},
                           "warnings": [], "save_config_pending": False,
                           "save_config_pending_items": {}}
    STATE["mcu"] = {"mcu_version": "v0.13.0-mock", "mcu_build_versions": "gcc: (GCC) 12.2.0",
                    "mcu_constants": {"MCU": "stm32f401xc", "CLOCK_FREQ": 84000000},
                    "last_stats": {"mcu_awake": 0.005, "mcu_task_avg": 1.2e-05, "freq": 84000000}}
    STATE["system_stats"] = {"sysload": 0.42, "cputime": 1.0, "memavail": 812000}
    STATE["toolhead"] = {"homed_axes": "", "position": [0.0, 0.0, 0.0, 0.0], "extruder": "extruder",
                         "print_time": 0.0, "estimated_print_time": 0.0, "stalls": 0,
                         "max_velocity": 500.0, "max_accel": 20000.0, "minimum_cruise_ratio": 0.5,
                         "square_corner_velocity": 5.0,
                         "axis_minimum": [0.0, 0.0, -2.0, 0.0], "axis_maximum": [256.0, 256.0, 256.0, 0.0]}
    STATE["gcode_move"] = {"speed_factor": 1.0, "speed": 1500.0, "extrude_factor": 1.0,
                           "absolute_coordinates": True, "absolute_extrude": True,
                           "homing_origin": [0.0, 0.0, 0.0, 0.0], "position": [0.0, 0.0, 0.0, 0.0],
                           "gcode_position": [0.0, 0.0, 0.0, 0.0],
                           "axis_map": {"X": 0, "Y": 1, "Z": 2, "E": 3}}
    for name in ext:
        c = CONFIG[name]
        STATE[name] = {"temperature": ROOM + 0.6, "target": 0.0, "power": 0.0, "can_extrude": False,
                       "pressure_advance": c["pressure_advance"],
                       "smooth_time": c["pressure_advance_smooth_time"], "motion_queue": None}
    STATE["heater_bed"] = {"temperature": ROOM - 0.9, "target": 0.0, "power": 0.0}
    if "heater_generic chamber_heater" in CONFIG:
        STATE["heater_generic chamber_heater"] = {"temperature": ROOM, "target": 0.0, "power": 0.0}
    STATE["temperature_fan chamber_fan"] = {"temperature": ROOM + 1, "target": 0.0, "speed": 0.0, "rpm": None}
    STATE["temperature_sensor chamber_temp"] = {"temperature": ROOM + 1, "measured_min_temp": ROOM,
                                                "measured_max_temp": ROOM + 1}
    sensors = ["temperature_fan chamber_fan", "temperature_sensor chamber_temp"]
    STATE["heaters"] = {"available_heaters": heaters(), "available_sensors": heaters() + sensors,
                        "available_monitors": []}
    for sec in CONFIG:
        kind = sec.split(" ")[0]
        if kind == "output_pin":
            STATE[sec] = {"value": 0.0}
        elif kind in ("fan", "fan_generic", "heater_fan", "controller_fan"):
            STATE[sec] = {"speed": 0.0, "rpm": None}
        elif kind == "led":
            STATE[sec] = {"color_data": [[0.0, 0.0, 0.0, 0.0]]}
    for m in MACROS:
        STATE["gcode_macro " + m] = {}
    STATE["print_stats"] = {"filename": "", "total_duration": 0.0, "print_duration": 0.0,
                            "filament_used": 0.0, "state": "standby", "message": "",
                            "info": {"total_layer": None, "current_layer": None}}
    STATE["virtual_sdcard"] = {"file_path": None, "progress": 0.0, "is_active": False,
                               "file_position": 0, "file_size": 0}
    STATE["pause_resume"] = {"is_paused": False}
    STATE["display_status"] = {"progress": 0.0, "message": None}
    STATE["idle_timeout"] = {"state": "Ready", "printing_time": 0.0, "idle_timeout": float(S.ARGS.idle_timeout)}
    STATE["motion_report"] = {"live_position": [0.0, 0.0, 0.0, 0.0], "live_velocity": 0.0,
                              "live_extruder_velocity": 0.0,
                              "steppers": ["extruder", "stepper_x", "stepper_y", "stepper_z"],
                              "trapq": ["extruder", "toolhead"]}
    STATE["exclude_object"] = {"objects": [], "excluded_objects": [], "current_object": None}
    STATE["manual_probe"] = {"is_active": False, "z_position": None,
                             "z_position_lower": None, "z_position_upper": None}
    STATE["screws_tilt_adjust"] = {"error": False, "max_deviation": None, "results": {}}
    MOTION.reset()
    SD.reset()


# ------------------------------------------------------------------ gcode parsing
TRADITIONAL = re.compile(r"^[A-Z][0-9]+(\.[0-9]*)?$")


class Cmd:
    def __init__(self, line):
        self.raw = line.strip()
        words = self.raw.split()
        self.name = words[0].upper() if words else ""
        self.params = {}
        if TRADITIONAL.match(self.name):
            for w in words[1:]:
                if w[0].isalpha():
                    self.params[w[0].upper()] = w[1:]
        else:
            for k, v in re.findall(r'(\S+?)=("[^"]*"|\'[^\']*\'|\S+)', self.raw[len(words[0]):] if words else ""):
                self.params[k.upper()] = v.strip("\"'")

    def get(self, key, default=KeyError):
        if key in self.params:
            return self.params[key]
        if default is KeyError:
            raise GcodeError("Error on '%s': missing %s" % (self.raw, key))
        return default

    def get_float(self, key, default=KeyError, minval=None, maxval=None):
        if key not in self.params:
            return self.get(key, default)
        v = self.params[key]
        try:
            v = float(v)
        except ValueError:
            raise GcodeError("Error on '%s': unable to parse %s" % (self.raw, self.params.get(key)))
        if minval is not None and v < minval:
            raise GcodeError("Error on '%s': %s must have minimum of %s" % (self.raw, key, minval))
        if maxval is not None and v > maxval:
            raise GcodeError("Error on '%s': %s must have maximum of %s" % (self.raw, key, maxval))
        return v

    def get_int(self, key, default=KeyError, minval=None, maxval=None):
        v = self.get_float(key, default, minval, maxval)
        return int(v) if v is not None else None


# ------------------------------------------------------------------ motion
class Motion:
    """gcode_move + toolhead: positions, coordinate modes, limits, timing."""

    def reset(self):
        self.base = [0.0] * 4
        self.saved = {}
        self.steppers_on = False
        self.last_active = time.monotonic()

    def last(self):
        return STATE["toolhead"]["position"]

    def update_status(self, pos=None):
        th, gm = STATE["toolhead"], STATE["gcode_move"]
        if pos is not None:
            th["position"] = [round(v, 4) for v in pos]
        gm["position"] = list(th["position"])
        gm["gcode_position"] = [round(p - b, 4) for p, b in zip(th["position"], self.base)]
        STATE["motion_report"]["live_position"] = list(th["position"])
        mark("toolhead", "gcode_move", "motion_report")

    def check(self, old, new):
        th = STATE["toolhead"]
        for i, axis in enumerate("xyz"):
            if abs(new[i] - old[i]) < 1e-9:
                continue
            if axis not in th["homed_axes"]:
                raise GcodeError("Must home axis first: %.3f %.3f %.3f [%.3f]" % tuple(new))
            if not th["axis_minimum"][i] <= new[i] <= th["axis_maximum"][i]:
                raise GcodeError("Move out of range: %.3f %.3f %.3f [%.3f]" % tuple(new))
        de = new[3] - old[3]
        if abs(de) > 1e-9:
            ext = STATE[th["extruder"]]
            if not ext["can_extrude"]:
                raise GcodeError("Extrude below minimum temp\n"
                                 "See the 'min_extrude_temp' config option for details")
            xyz = math.dist(old[:3], new[:3])
            limit = CONFIG[th["extruder"]]["max_extrude_only_distance"]
            if xyz < 1e-9 and abs(de) > limit:
                raise GcodeError("Extrude only move too long (%.3fmm vs %.3fmm)\n"
                                 "See the 'max_extrude_only_distance' config option for details"
                                 % (abs(de), limit))

    def move(self, cmd):
        """G0/G1. Returns the move's duration in seconds."""
        gm = STATE["gcode_move"]
        old = list(self.last())
        new = list(old)
        for i, axis in enumerate("XYZE"):
            if axis not in cmd.params:
                continue
            try:
                v = float(cmd.params[axis])
            except ValueError:
                raise GcodeError("Unable to parse move '%s'" % cmd.raw)
            absolute = gm["absolute_coordinates"]
            if axis == "E":
                v *= gm["extrude_factor"]
                absolute = absolute and gm["absolute_extrude"]
            new[i] = new[i] + v if not absolute else v + self.base[i]
        if "F" in cmd.params:
            f = float(cmd.params["F"])
            if f <= 0:
                raise GcodeError("Invalid speed in '%s'" % cmd.raw)
            gm["speed"] = f
        self.check(old, new)
        self.steppers_on = True
        speed = min(gm["speed"] * gm["speed_factor"] / 60.0, CONFIG["printer"]["max_velocity"])
        dist = math.dist(old[:3], new[:3]) or abs(new[3] - old[3])
        duration = dist / speed if speed > 0 else 0.0
        de = new[3] - old[3]
        mr = STATE["motion_report"]
        mr["live_velocity"] = round(speed if math.dist(old[:3], new[:3]) > 0 else 0.0, 2)
        mr["live_extruder_velocity"] = round(de / duration, 3) if duration > 0 else 0.0
        self.update_status(new)
        if SD.stats.start is not None and SD.stats.pause_at is None:
            # print_stats._update_filament_usage: the extruder's net travel
            STATE["print_stats"]["filament_used"] += de / gm["extrude_factor"]
        if de > 0:
            mmu.extruded(de)
        return duration


MOTION = Motion()


# ------------------------------------------------------------------ print stats
class PrintStats:
    """print_stats' note_* bookkeeping, on the simulated print clock."""

    def __init__(self):
        self.start = self.pause_at = None
        self.paused_total = self.init_duration = 0.0

    def ps(self):
        return STATE["print_stats"]

    def reset(self):
        self.start = self.pause_at = None
        self.paused_total = self.init_duration = 0.0
        ps = STATE.get("print_stats")
        if ps is None:
            return
        ps.update(filename="", message="", state="standby", total_duration=0.0,
                  print_duration=0.0, filament_used=0.0,
                  info={"total_layer": None, "current_layer": None})

    def tick(self, now):
        ps = self.ps()
        if self.start is None:
            return
        paused = self.paused_total + (now - self.pause_at if self.pause_at is not None else 0.0)
        ps["total_duration"] = round(now - self.start, 2)
        if ps["filament_used"] < 1e-7:
            self.init_duration = ps["total_duration"] - paused
        ps["print_duration"] = round(max(0.0, ps["total_duration"] - self.init_duration - paused), 2)

    def note_start(self, now):
        if self.start is None:
            self.start = now
        elif self.pause_at is not None:
            self.paused_total += now - self.pause_at
            self.pause_at = None
        self.ps().update(state="printing", message="")

    def note_pause(self, now):
        if self.pause_at is None:
            self.pause_at = now
        self.ps()["state"] = "paused"

    def finish(self, state, now, message=""):
        if self.start is None:
            return
        self.tick(now)
        self.ps().update(state=state, message=message)
        self.start = self.pause_at = None


# ------------------------------------------------------------------ virtual sdcard
class VirtualSD:
    """Runs a gcode file a line at a time, on a clock ARGS.speed times faster
    than real time, exactly as virtual_sdcard.work_handler sequences it."""

    def __init__(self):
        self.stats = PrintStats()
        self.reset()

    def reset(self):
        self.path = None
        self.data = b""
        self.task = None
        self.must_pause = False
        self.sd_paused = False
        self.from_sd = False
        self.lock_wait = False
        self.clock = 0.0
        self.stats.reset()

    def now(self):
        return self.clock

    def active(self):
        return self.task is not None

    def status(self):
        sd = STATE["virtual_sdcard"]
        sd.update(file_path=self.path, is_active=self.active(),
                  file_size=len(self.data) if self.path else 0)
        sd["progress"] = round(sd["file_position"] / sd["file_size"], 4) if sd["file_size"] else 0.0
        mark("virtual_sdcard", "print_stats")

    def reset_file(self):
        self.path, self.data = None, b""
        STATE["virtual_sdcard"]["file_position"] = 0
        self.stats.reset()
        STATE["exclude_object"].update(objects=[], excluded_objects=[], current_object=None)
        mark("exclude_object")
        self.status()

    async def print_file(self, filename):
        if self.active():
            raise GcodeError("SD busy")
        self.reset_file()
        filename = filename.lstrip("/")
        path = CONFIG["virtual_sdcard"]["path"] + "/" + filename
        try:
            with open(path, "rb") as f:
                self.data = f.read()
        except OSError:
            raise GcodeError("Unable to open file")
        self.path = path
        self.stats.ps()["filename"] = filename
        await console("File opened:%s Size:%d" % (filename, len(self.data)))
        await console("File selected")
        await HOOKS["print_event"]("started")
        await self.resume()

    async def resume(self):
        if self.active():
            if not (self.must_pause and self.lock_wait):
                raise GcodeError("SD busy")
            # paused before it could run another line: still queued on the mutex
            self.task.cancel()
            self.task, self.lock_wait = None, False
        self.must_pause = False
        self.task = asyncio.create_task(self.work())
        self.status()

    async def pause(self):
        if self.active():
            self.must_pause = True
            while self.active() and not self.from_sd and not self.lock_wait:
                await asyncio.sleep(0.01)

    async def cancel(self):
        if self.path is not None:
            await self.pause()
            self.path = None
            self.stats.finish("cancelled", self.now())
            await HOOKS["print_event"]("cancelled")
        STATE["virtual_sdcard"]["file_position"] = 0
        self.data = b""
        self.status()

    async def work(self):
        sd = STATE["virtual_sdcard"]
        pos = sd["file_position"]
        self.stats.note_start(self.now())
        await HOOKS["print_event"]("printing")
        STATE["idle_timeout"]["state"] = "Printing"
        mark("idle_timeout")
        error = None
        debt = 0.0
        speed = max(0.1, float(S.ARGS.speed))
        while not self.must_pause:
            if pos >= len(self.data):
                self.path = None
                await console("Done printing file")
                break
            end = self.data.find(b"\n", pos)
            end = len(self.data) if end < 0 else end
            line = self.data[pos:end].decode("utf-8", "replace")
            nxt = end + 1
            try:
                took = await HOOKS["sd_line"](line)
                if took is None:
                    break
                debt += took
            except GcodeError as e:
                error = e.message
                await console("!! " + error)
                try:
                    await run_script(CONFIG["virtual_sdcard"]["on_error_gcode"])
                except GcodeError:
                    pass
                break
            pos = sd["file_position"] = nxt
            if debt > 0.02 * speed:
                self.clock += debt
                self.stats.tick(self.clock)
                self.status()
                await asyncio.sleep(debt / speed)
                debt = 0.0
        self.clock += debt
        self.task = None
        if error is not None:
            self.stats.finish("error", self.now(), error)
            await HOOKS["print_event"]("error")
        elif self.path is not None:
            self.stats.note_pause(self.now())
        else:
            self.stats.finish("complete", self.now())
            await HOOKS["print_event"]("complete")
        STATE["motion_report"].update(live_velocity=0.0, live_extruder_velocity=0.0)
        STATE["idle_timeout"]["state"] = "Ready"
        MOTION.last_active = time.monotonic()
        mark("motion_report", "idle_timeout")
        self.status()

    async def wait(self, seconds):
        """Real-time waits during a print (heating) still advance the print clock."""
        await asyncio.sleep(seconds)
        if self.active() or self.from_sd:
            self.clock += seconds * float(S.ARGS.speed)
            self.stats.tick(self.clock)
            self.status()


SD = VirtualSD()


# ------------------------------------------------------------------ heaters
async def set_target(name, target):
    if name not in heaters():
        raise GcodeError("Unknown heater '%s'" % name)
    lo, hi = CONFIG[name]["min_temp"], CONFIG[name]["max_temp"]
    if target and (target < lo or target > hi):
        raise GcodeError("Requested temperature (%.1f) out of range (%.1f:%.1f)" % (target, lo, hi))
    STATE[name]["target"] = float(target)
    mark(name)


async def wait_temp(name):
    """M109/M190: klipper reports temps while it waits."""
    h = STATE[name]
    while h["target"] > 0 and abs(h["temperature"] - h["target"]) > 1.0:
        if S.KLIPPY["state"] != "ready":
            return
        await console(temp_report())
        await SD.wait(1.0)


def temp_report():
    parts = []
    for i, name in enumerate(extruder_names()):
        h = STATE[name]
        parts.append("%s:%.1f /%.1f" % ("T%d" % i if len(extruder_names()) > 1 else "T",
                                         h["temperature"], h["target"]))
    b = STATE["heater_bed"]
    parts.append("B:%.1f /%.1f" % (b["temperature"], b["target"]))
    return " ".join(parts)


async def physics():
    """One real-time clock for everything that moves on its own."""
    t = 0
    while True:
        await asyncio.sleep(1)
        t += 1
        if S.KLIPPY["state"] != "ready":
            continue
        diff = {}
        for name in heaters():
            h = STATE[name]
            rate = {"heater_bed": 3.5, "heater_generic chamber_heater": 0.8}.get(name, 11.0)
            target, now = h["target"], h["temperature"]
            if target > 0 and now < target - 0.5:
                h["power"] = round(min(1.0, 0.35 + (target - now) / 25), 3)
                now += rate * h["power"]
            elif target > 0:
                h["power"] = 0.22
                now += (target - now) * 0.4 + math.sin(t / 4) * 0.1
            else:
                h["power"] = 0.0
                now += (ROOM - now) * (0.02 if name == "heater_bed" else 0.04)
            h["temperature"] = round(now, 2)
            if name in CONFIG and "min_extrude_temp" in CONFIG[name]:
                h["can_extrude"] = now >= CONFIG[name]["min_extrude_temp"]
            diff[name] = h
        # the bed (and the chamber heater) warm the enclosure; the fan pulls it down
        fan = STATE["temperature_fan chamber_fan"]
        fan["speed"] = 1.0 if 0 < fan["target"] < fan["temperature"] else 0.0
        warm = max(0.0, STATE["heater_bed"]["temperature"] - ROOM) * 0.25
        if "heater_generic chamber_heater" in STATE:
            warm += max(0.0, STATE["heater_generic chamber_heater"]["temperature"] - ROOM) * 0.8
        goal = ROOM + (warm * 0.3 if fan["speed"] else warm)
        fan["temperature"] = round(fan["temperature"] + (goal - fan["temperature"]) * 0.05, 2)
        ct = STATE["temperature_sensor chamber_temp"]
        ct["temperature"] = fan["temperature"]
        ct["measured_min_temp"] = round(min(ct["measured_min_temp"], ct["temperature"]), 2)
        ct["measured_max_temp"] = round(max(ct["measured_max_temp"], ct["temperature"]), 2)
        diff["temperature_fan chamber_fan"] = fan
        diff["temperature_sensor chamber_temp"] = ct
        # heater_fan / controller_fan follow their heaters and the steppers
        hot = any(STATE[n]["target"] > 0 for n in heaters())
        if hot or SD.active():
            MOTION.last_active = time.monotonic()
        for sec, c in CONFIG.items():
            if sec.startswith("heater_fan "):
                on = any(STATE[h]["target"] > 0 or STATE[h]["temperature"] > c["heater_temp"] for h in c["heater"])
                STATE[sec]["speed"] = c["fan_speed"] if on else 0.0
                diff[sec] = STATE[sec]
            elif sec.startswith("controller_fan "):
                busy = MOTION.steppers_on or hot
                idle = time.monotonic() - MOTION.last_active
                STATE[sec]["speed"] = c["fan_speed"] if busy or idle < c["idle_timeout"] else 0.0
                diff[sec] = STATE[sec]
        it = STATE["idle_timeout"]
        if SD.active():
            it["printing_time"] = round(it["printing_time"] + 1, 1)
        elif it["state"] == "Ready" and time.monotonic() - MOTION.last_active > S.ARGS.idle_timeout:
            it.update(state="Idle", printing_time=0.0)
            await run_script(CONFIG["idle_timeout"]["gcode"])
        diff["idle_timeout"] = it
        STATE["system_stats"]["sysload"] = round(0.35 + 0.2 * abs(math.sin(t / 30)) + (0.4 if SD.active() else 0), 2)
        STATE["system_stats"]["cputime"] = round(STATE["system_stats"]["cputime"] + 0.03, 2)
        diff["system_stats"] = STATE["system_stats"]
        mark(*diff)


# ------------------------------------------------------------------ klippy lifecycle
async def set_klippy(state, message):
    S.KLIPPY.update(state=state, message=message)
    if "webhooks" in STATE:
        STATE["webhooks"].update(state=state, state_message=message)
        mark("webhooks")
    await HOOKS["klippy_state"](state)


async def shutdown(reason):
    """klippy:shutdown: heaters off, the print stops where it is."""
    if S.KLIPPY["state"] == "shutdown":
        return
    for name in heaters():
        STATE[name]["target"] = 0.0
    if SD.active():
        SD.must_pause = True
    await console("!! " + reason)
    await set_klippy("shutdown", reason + "\nOnce the underlying issue is corrected, use the\n"
                     '"FIRMWARE_RESTART" command to reset the firmware, reload the\n'
                     "config, and restart the host software.\nPrinter is shutdown")


async def restart():
    """RESTART / FIRMWARE_RESTART: klippy goes away and comes back fresh."""
    if SD.task:
        SD.task.cancel()
        SD.task = None
    await set_klippy("disconnected", "Klippy Disconnected")
    await asyncio.sleep(1.0)
    await set_klippy("startup", "Klipper is starting")
    await asyncio.sleep(1.5)
    await start()


async def start():
    build_config()
    build_objects(keep=mmu.objects())
    await set_klippy("ready", "Printer is ready")


# ------------------------------------------------------------------ filament macros
async def extrude_e(length, speed_mm_min):
    """A relative E move in 10mm pieces, so the flow readout moves with it."""
    remaining = abs(length)
    sign = 1 if length > 0 else -1
    while remaining > 1e-6:
        step = min(10.0, remaining)
        remaining -= step
        dur = MOTION.move(Cmd("G1 E%.3f F%.0f" % (sign * step, speed_mm_min)))
        mark("motion_report")
        await asyncio.sleep(dur)
    STATE["motion_report"]["live_extruder_velocity"] = 0.0
    mark("motion_report")


async def filament_macro(cmd):
    temp = cmd.get_float("EXTRUDER_TEMP", 0.0) or 0.0
    length = cmd.get_float("EXTRUDE_LEN", 80.0)
    speed = cmd.get_float("SPEED", 300.0) or 300.0
    if cmd.name in ("_GUPPYSCREEN_RETRACT", "UNLOAD_FILAMENT"):
        length = -length
    ext = STATE["toolhead"]["extruder"]
    if temp:
        await set_target(ext, temp)
    if STATE[ext]["target"] < CONFIG[ext]["min_extrude_temp"]:
        raise GcodeError("Extrude below minimum temp\nSee the 'min_extrude_temp' config option for details")
    await respond_info("Heating %s to %.0f" % (ext, STATE[ext]["target"]))
    await wait_temp(ext)
    gm = STATE["gcode_move"]
    saved = (gm["absolute_extrude"], gm["speed"])
    gm["absolute_extrude"] = False
    try:
        await extrude_e(length, speed)
    finally:
        gm["absolute_extrude"], gm["speed"] = saved


# ------------------------------------------------------------------ dispatch
async def run_script(script):
    for line in script.split("\n"):
        await run_line(line)


def strip_comment(line):
    i = line.find(";")
    return (line[:i] if i >= 0 else line).strip()


async def run_line(line, from_sd=False):
    """Execute one line of gcode. Returns simulated seconds it took."""
    line = strip_comment(line)
    if not line:
        return 0.0
    cmd = Cmd(line)
    if S.KLIPPY["state"] != "ready" and cmd.name not in (
            "FIRMWARE_RESTART", "RESTART", "STATUS", "HELP", "M115", "M112",
            "SIM_READY", "SIM_DISCONNECT", "SIM_KLIPPER_STOP"):
        raise GcodeError(S.KLIPPY["message"].split("\n")[0] if S.KLIPPY["state"] == "shutdown"
                         else "Klipper is not ready")
    eo = STATE["exclude_object"]
    if (eo["current_object"] in eo["excluded_objects"] and cmd.name in ("G0", "G1")):
        # an excluded object's moves are dropped, but the position stays known
        pos = MOTION.last()
        for i, axis in enumerate("XYZ"):
            if axis in cmd.params:
                v = float(cmd.params[axis])
                pos[i] = v + MOTION.base[i] if STATE["gcode_move"]["absolute_coordinates"] else pos[i] + v
        MOTION.update_status()
        return 0.0
    if not from_sd:
        MOTION.last_active = time.monotonic()
        if STATE["idle_timeout"]["state"] == "Idle":
            STATE["idle_timeout"]["state"] = "Ready"
            mark("idle_timeout")
    handler = HANDLERS.get(cmd.name)
    if handler is None:
        if await mmu.handle(cmd):
            return 0.0
        if re.fullmatch(r"T\d+", cmd.name):
            await mmu.tool(int(cmd.name[1:]))
            return 0.0
        await respond_info('Unknown command:"%s"' % cmd.name)
        return 0.0
    result = await handler(cmd)
    return result or 0.0


HANDLERS = {}


def gcode(*names):
    def deco(fn):
        for n in names:
            HANDLERS[n] = fn
        return fn
    return deco


def mux(cmd, key, prefixes, strip=True):
    """gcode.py's _cmd_mux: the object a KEY=name parameter names, or its error."""
    objs = {(k.split(" ", 1)[1] if strip else k): k for k in STATE if k.split(" ")[0] in prefixes}
    value = cmd.get(key)
    if value not in objs:
        guess = next((n for n in objs if value and value in n), None)
        raise GcodeError("The value '%s' is not valid for %s%s" % (value, key, (
            ". Did you mean '%s'?" % guess if guess else
            ". Options: %s" % ", ".join("'%s'" % n for n in objs))))
    return objs[value]


@gcode("G0", "G1")
async def g_move(cmd):
    dur = MOTION.move(cmd)
    if not SD.from_sd and dur > 0:
        mark("toolhead", "gcode_move")
        await asyncio.sleep(min(dur, 3.0))
        STATE["motion_report"].update(live_velocity=0.0, live_extruder_velocity=0.0)
        mark("motion_report")
    return dur


@gcode("G4")
async def g_dwell(cmd):
    ms = cmd.get_float("P", 0.0, minval=0.0)
    return ms / 1000.0


@gcode("G28")
async def g_home(cmd):
    axes = [a for a in "xyz" if a.upper() in cmd.params] or list("xyz")
    th = STATE["toolhead"]
    MOTION.steppers_on = True
    for axis in axes:
        th["homed_axes"] = "".join(a for a in "xyz" if a in th["homed_axes"] and a != axis)
        mark("toolhead")
        await (SD.wait(0.6) if SD.from_sd else asyncio.sleep(0.6))
        pos = list(th["position"])
        pos["xyz".index(axis)] = CONFIG["stepper_" + axis]["position_endstop"]
        th["homed_axes"] = "".join(a for a in "xyz" if a in th["homed_axes"] or a == axis)
        MOTION.update_status(pos)
        mark("toolhead", "gcode_move")
    return 0.0


PAST_Z = []            # heights TESTZ has left, for the bisect steps


@gcode("PROBE_CALIBRATE", "Z_ENDSTOP_CALIBRATE")
async def g_manual_probe(cmd):
    mp = STATE["manual_probe"]
    if mp["is_active"]:
        raise GcodeError("Already in a manual Z probe. Use ABORT to abort it.")
    mp.update(is_active=True, z_position=5.0, z_position_lower=None, z_position_upper=None)
    PAST_Z.clear()
    mark("manual_probe")
    await respond_info("Starting manual Z probe. Use TESTZ to adjust position.\n"
                       "Finish with ACCEPT or ABORT command.")


@gcode("SCREWS_TILT_CALCULATE")
async def g_screws_tilt(cmd):
    st = STATE["screws_tilt_adjust"]
    md = cmd.get("MAX_DEVIATION", None)
    st["max_deviation"] = float(md) if md is not None else None
    await asyncio.sleep(2.0)  # probing every screw
    base = 2.5
    results = {"screw1": {"z": base, "sign": "CW", "adjust": "00:00", "is_base": True}}
    lines = ["%s (base) : x=%.1f, y=%.1f, z=%.5f" % (SCREWS[0] + (base,))]
    for i, (name, x, y) in enumerate(SCREWS[1:], 2):
        z = round(base + random.uniform(-0.3, 0.3), 5)
        turns = abs(z - base) / 0.5  # M3: 0.5mm a turn
        adjust = "%02d:%02d" % (int(turns), round((turns - int(turns)) * 60))
        sign = "CW" if z > base else "CCW"
        results["screw%d" % i] = {"z": z, "sign": sign, "adjust": adjust, "is_base": False}
        lines.append("%s : x=%.1f, y=%.1f, z=%.5f : adjust %s %s" % (name, x, y, z, sign, adjust))
    st.update(results=results, error=False)
    mark("screws_tilt_adjust")
    await respond_info("\n".join(lines))


@gcode("TESTZ", "ACCEPT", "ABORT")
async def g_manual_probe_step(cmd):
    mp = STATE["manual_probe"]
    if not mp["is_active"]:
        await respond_info('Unknown command:"%s"' % cmd.name)
        return
    if cmd.name == "TESTZ":
        # manual_probe.py's cmd_TESTZ: +/- bisect towards the heights already
        # tried, ++/-- go to them, at most 0.2 at a time
        z, pp = mp["z_position"], PAST_Z
        if z not in pp:
            pp.append(z)
            pp.sort()
        i = pp.index(z)
        req = cmd.get("Z")
        if req in ("+", "++"):
            check = pp[i + 1] if i + 1 < len(pp) else 1e12
            nz = min((check + z) / 2 if req == "+" else check, z + 0.2)
        elif req in ("-", "--"):
            check = pp[i - 1] if i > 0 else -1e12
            nz = max((check + z) / 2 if req == "-" else check, z - 0.2)
        else:
            nz = z + cmd.get_float("Z")
        nz = round(max(nz, -2.0), 6)
        lower = [p for p in pp if p < nz]
        upper = [p for p in pp if p > nz]
        mp.update(z_position=nz, z_position_lower=lower[-1] if lower else None,
                  z_position_upper=upper[0] if upper else None)
        await respond_info("Z position: %.3f" % nz)
    elif cmd.name == "ACCEPT":
        await respond_info("probe: z_offset: %.3f\nThe SAVE_CONFIG command will update the printer config\n"
                           "file with the above and restart the printer." % mp["z_position"])
        mp.update(is_active=False, z_position=None, z_position_lower=None, z_position_upper=None)
    else:
        await respond_info("Manual probe failed")
        mp.update(is_active=False, z_position=None, z_position_lower=None, z_position_upper=None)
    mark("manual_probe")


@gcode("G90", "G91")
async def g_coords(cmd):
    STATE["gcode_move"]["absolute_coordinates"] = cmd.name == "G90"
    mark("gcode_move")


@gcode("M82", "M83")
async def g_ecoords(cmd):
    STATE["gcode_move"]["absolute_extrude"] = cmd.name == "M82"
    mark("gcode_move")


@gcode("G92")
async def g_setpos(cmd):
    pos = MOTION.last()
    given = [i for i, a in enumerate("XYZE") if a in cmd.params]
    for i in given or range(4):
        v = float(cmd.params.get("XYZE"[i], 0.0))
        if i == 3:
            v *= STATE["gcode_move"]["extrude_factor"]
        MOTION.base[i] = pos[i] - v
    MOTION.update_status()


@gcode("M84", "M18")
async def g_motors_off(cmd):
    STATE["toolhead"]["homed_axes"] = ""
    MOTION.steppers_on = False
    mark("toolhead")


@gcode("M104", "M109")
async def g_extruder_temp(cmd):
    ext = STATE["toolhead"]["extruder"]
    if "T" in cmd.params:
        idx = cmd.get_int("T", 0)
        ext = "extruder" if idx == 0 else "extruder%d" % idx
        if ext not in STATE:
            if cmd.name == "M104" and not cmd.get_float("S", 0.0):
                return
            raise GcodeError("Invalid extruder index: %d" % idx)
    await set_target(ext, cmd.get_float("S", 0.0))
    if cmd.name == "M109":
        await wait_temp(ext)


@gcode("M140", "M190")
async def g_bed_temp(cmd):
    await set_target("heater_bed", cmd.get_float("S", 0.0))
    if cmd.name == "M190":
        await wait_temp("heater_bed")


@gcode("SET_HEATER_TEMPERATURE")
async def g_set_heater(cmd):
    await set_target(cmd.get("HEATER"), cmd.get_float("TARGET", 0.0))


@gcode("TURN_OFF_HEATERS")
async def g_heaters_off(cmd):
    for name in heaters():
        STATE[name]["target"] = 0.0
    mark(*heaters())


@gcode("SET_TEMPERATURE_FAN_TARGET")
async def g_tempfan(cmd):
    key = mux(cmd, "TEMPERATURE_FAN", ("temperature_fan",))
    c = CONFIG[key]
    target = cmd.get_float("TARGET", STATE[key]["target"])
    if target and (target < c["min_temp"] or target > c["max_temp"]):
        raise GcodeError("Requested temperature (%.1f) out of range (%.1f:%.1f)"
                         % (target, c["min_temp"], c["max_temp"]))
    STATE[key]["target"] = target
    mark(key)


@gcode("M105")
async def g_m105(cmd):
    await console("ok " + temp_report())


@gcode("M114")
async def g_m114(cmd):
    await console("X:%.3f Y:%.3f Z:%.3f E:%.3f" % tuple(STATE["gcode_move"]["gcode_position"]))


@gcode("M115")
async def g_m115(cmd):
    await console("FIRMWARE_NAME:Klipper FIRMWARE_VERSION:v0.13.0-mock")


@gcode("M117", "M118")
async def g_m117(cmd):
    text = cmd.raw[4:].strip()
    if cmd.name == "M117":
        STATE["display_status"]["message"] = text or None
        mark("display_status")
    else:
        await console(text)


@gcode("M106", "M107")
async def g_part_fan(cmd):
    if "fan" in STATE:
        s = cmd.get_float("S", 255.0, minval=0.0) if cmd.name == "M106" else 0.0
        STATE["fan"]["speed"] = round(s / 255.0, 4)
        mark("fan")


@gcode("SET_FAN_SPEED")
async def g_fan_speed(cmd):
    key = mux(cmd, "FAN", ("fan_generic",))
    STATE[key]["speed"] = round(cmd.get_float("SPEED", 0.0, minval=0.0, maxval=1.0), 4)
    mark(key)


@gcode("SET_PIN")
async def g_set_pin(cmd):
    key = mux(cmd, "PIN", ("output_pin",))
    c = CONFIG[key]
    value = cmd.get_float("VALUE", None, minval=0.0, maxval=c["scale"])
    if value is None:
        raise GcodeError("SET_PIN command must specify VALUE or TEMPLATE")
    value /= c["scale"]
    if not c["pwm"] and value not in (0.0, 1.0):
        raise GcodeError("Invalid pin value")
    STATE[key]["value"] = round(value, 4)
    mark(key)


@gcode("SET_LED")
async def g_set_led(cmd):
    key = mux(cmd, "LED", ("led",))
    color = [round(cmd.get_float(c, 0.0, minval=0.0, maxval=1.0), 4) for c in ("RED", "GREEN", "BLUE", "WHITE")]
    STATE[key]["color_data"] = [color] * len(STATE[key]["color_data"])
    mark(key)


@gcode("M220", "M221")
async def g_factor(cmd):
    s = cmd.get_float("S", 100.0)
    if s <= 0:
        raise GcodeError("Error on '%s': S must be above 0.0" % cmd.raw)
    STATE["gcode_move"]["speed_factor" if cmd.name == "M220" else "extrude_factor"] = s / 100.0
    mark("gcode_move")


@gcode("SET_GCODE_OFFSET")
async def g_offset(cmd):
    gm = STATE["gcode_move"]
    delta = [0.0] * 4
    for i, axis in enumerate("XYZE"):
        if axis in cmd.params:
            delta[i] = cmd.get_float(axis) - gm["homing_origin"][i]
        elif axis + "_ADJUST" in cmd.params:
            delta[i] = cmd.get_float(axis + "_ADJUST")
    for i in range(4):
        gm["homing_origin"][i] = round(gm["homing_origin"][i] + delta[i], 4)
        MOTION.base[i] += delta[i]
    if cmd.get_int("MOVE", 0):
        pos = MOTION.last()
        MOTION.update_status([p + d for p, d in zip(pos, delta)])
    else:
        MOTION.update_status()
    mark("gcode_move", "toolhead")


@gcode("SAVE_GCODE_STATE")
async def g_save_state(cmd):
    gm = STATE["gcode_move"]
    MOTION.saved[cmd.get("NAME", "default")] = dict(
        absolute_coordinates=gm["absolute_coordinates"], absolute_extrude=gm["absolute_extrude"],
        speed=gm["speed"], speed_factor=gm["speed_factor"], extrude_factor=gm["extrude_factor"],
        base=list(MOTION.base), position=list(MOTION.last()))


@gcode("RESTORE_GCODE_STATE")
async def g_restore_state(cmd):
    name = cmd.get("NAME", "default")
    st = MOTION.saved.get(name)
    if st is None:
        raise GcodeError("Unknown g-code state: %s" % name)
    gm = STATE["gcode_move"]
    for k in ("absolute_coordinates", "absolute_extrude", "speed", "speed_factor", "extrude_factor"):
        gm[k] = st[k]
    MOTION.base = list(st["base"])
    if cmd.get_int("MOVE", 0):
        pos = list(MOTION.last())
        pos[:3] = st["position"][:3]
        MOTION.update_status(pos)
    mark("gcode_move")


@gcode("SET_PRESSURE_ADVANCE")
async def g_pa(cmd):
    cmd.params.setdefault("EXTRUDER", STATE["toolhead"]["extruder"])
    name = mux(cmd, "EXTRUDER", ("extruder", "extruder1", "extruder2", "extruder3"), strip=False)
    e = STATE[name]
    e["pressure_advance"] = cmd.get_float("ADVANCE", e["pressure_advance"], minval=0.0)
    e["smooth_time"] = cmd.get_float("SMOOTH_TIME", e["smooth_time"], minval=0.0, maxval=0.2)
    mark(name)
    await respond_info("pressure_advance: %.6f\npressure_advance_smooth_time: %.6f"
                       % (e["pressure_advance"], e["smooth_time"]))


@gcode("ACTIVATE_EXTRUDER")
async def g_activate(cmd):
    name = mux(cmd, "EXTRUDER", ("extruder", "extruder1", "extruder2", "extruder3"), strip=False)
    th = STATE["toolhead"]
    if th["extruder"] == name:
        await respond_info("Extruder %s already active" % name)
        return
    await respond_info("Activating extruder %s" % name)
    th["extruder"] = name
    mark("toolhead")


@gcode("PAUSE")
async def g_pause(cmd):
    pr = STATE["pause_resume"]
    if pr["is_paused"]:
        await respond_info("Print already paused")
        return
    if SD.active():
        SD.sd_paused = True
        await SD.pause()
        SD.stats.note_pause(SD.now())
        await HOOKS["print_event"]("paused")
    else:
        SD.sd_paused = False
        await respond_info("action:paused")
    await g_save_state(Cmd("SAVE_GCODE_STATE NAME=PAUSE_STATE"))
    pr["is_paused"] = True
    mark("pause_resume", "print_stats")


@gcode("RESUME")
async def g_resume(cmd):
    pr = STATE["pause_resume"]
    if not pr["is_paused"]:
        await respond_info("Print is not paused, resume aborted")
        return
    await g_restore_state(Cmd("RESTORE_GCODE_STATE NAME=PAUSE_STATE MOVE=1"))
    await mmu.on_resume()
    pr["is_paused"] = False
    if SD.sd_paused:
        SD.sd_paused = False
        await SD.resume()
        await HOOKS["print_event"]("resumed")
    else:
        await respond_info("action:resumed")
    mark("pause_resume", "print_stats")


@gcode("CLEAR_PAUSE")
async def g_clear_pause(cmd):
    STATE["pause_resume"]["is_paused"] = False
    mark("pause_resume")


@gcode("CANCEL_PRINT")
async def g_cancel(cmd):
    if SD.active() or SD.path:
        await SD.cancel()
    else:
        await respond_info("action:cancel")
    SD.sd_paused = False
    STATE["pause_resume"]["is_paused"] = False
    mark("pause_resume", "print_stats")


@gcode("SDCARD_PRINT_FILE")
async def g_print_file(cmd):
    await SD.print_file(cmd.get("FILENAME"))


@gcode("SDCARD_RESET_FILE")
async def g_reset_file(cmd):
    if SD.from_sd:
        raise GcodeError("SDCARD_RESET_FILE cannot be run from the sdcard")
    await SD.pause()
    SD.reset_file()


@gcode("SET_PRINT_STATS_INFO")
async def g_stats_info(cmd):
    info = STATE["print_stats"]["info"]
    total = cmd.get_int("TOTAL_LAYER", info["total_layer"], minval=0)
    current = cmd.get_int("CURRENT_LAYER", info["current_layer"], minval=0)
    if total == 0:
        info.update(total_layer=None, current_layer=None)
    elif total != info["total_layer"]:
        info.update(total_layer=total, current_layer=0)
    if info["total_layer"] is not None and current is not None and current != info["current_layer"]:
        info["current_layer"] = min(current, info["total_layer"])
    mark("print_stats")


@gcode("EXCLUDE_OBJECT_DEFINE")
async def g_eo_define(cmd):
    eo = STATE["exclude_object"]
    if "RESET" in cmd.params:
        eo.update(objects=[], excluded_objects=[], current_object=None)
    elif "NAME" in cmd.params:
        obj = {"name": cmd.params["NAME"].upper()}
        if "CENTER" in cmd.params:
            obj["center"] = json.loads("[%s]" % cmd.params["CENTER"])
        if "POLYGON" in cmd.params:
            obj["polygon"] = json.loads(cmd.params["POLYGON"])
        eo["objects"] = sorted(eo["objects"] + [obj], key=lambda o: o["name"])
    else:
        await respond_info("Known objects: %s" % " ".join(o["name"] for o in eo["objects"]))
        return
    mark("exclude_object")


@gcode("EXCLUDE_OBJECT_START")
async def g_eo_start(cmd):
    eo = STATE["exclude_object"]
    name = cmd.get("NAME").upper()
    if not any(o["name"] == name for o in eo["objects"]):
        eo["objects"] = sorted(eo["objects"] + [{"name": name}], key=lambda o: o["name"])
    eo["current_object"] = name
    mark("exclude_object")


@gcode("EXCLUDE_OBJECT_END")
async def g_eo_end(cmd):
    eo = STATE["exclude_object"]
    name = cmd.get("NAME", None)
    if name is not None and name.upper() != eo["current_object"]:
        await respond_info("EXCLUDE_OBJECT_END NAME=%s does not match the current object NAME=%s"
                           % (name.upper(), eo["current_object"]))
    eo["current_object"] = None
    mark("exclude_object")


@gcode("EXCLUDE_OBJECT")
async def g_exclude(cmd):
    eo = STATE["exclude_object"]
    name = cmd.get("NAME", "").upper()
    if "RESET" in cmd.params:
        if name:
            await respond_info("Unexcluding object %s" % name)
            eo["excluded_objects"] = sorted(n for n in eo["excluded_objects"] if n != name)
        else:
            eo["excluded_objects"] = []
    elif name:
        if name not in eo["excluded_objects"]:
            await respond_info("Excluding object %s" % name)
            eo["excluded_objects"] = sorted(eo["excluded_objects"] + [name])
    elif "CURRENT" in cmd.params:
        if not eo["current_object"]:
            raise GcodeError("There is no current object to cancel")
        await respond_info("Excluding object %s" % eo["current_object"])
        eo["excluded_objects"] = sorted(eo["excluded_objects"] + [eo["current_object"]])
    else:
        await respond_info("Excluded objects: %s" % " ".join(eo["excluded_objects"]))
        return
    mark("exclude_object")


@gcode("_GUPPYSCREEN_EXTRUDE", "_GUPPYSCREEN_RETRACT", "LOAD_FILAMENT", "UNLOAD_FILAMENT")
async def g_filament(cmd):
    await filament_macro(cmd)


@gcode("_CLIENT_LINEAR_MOVE")
async def g_client_move(cmd):
    """Mainsail/Fluidd client.cfg macro: a relative move, gcode state kept."""
    await g_save_state(Cmd("SAVE_GCODE_STATE NAME=_client_movement"))
    STATE["gcode_move"]["absolute_coordinates"] = False
    try:
        args = " ".join("%s%s" % (a, cmd.params[a]) for a in "XYZEF" if a in cmd.params)
        await g_move(Cmd("G1 " + args))
    finally:
        await g_restore_state(Cmd("RESTORE_GCODE_STATE NAME=_client_movement"))


@gcode("SAVE_CONFIG")
async def g_save_config(cmd):
    await respond_info("No changes to save to config" if not STATE["configfile"]["save_config_pending"]
                       else "Saving config and restarting")
    asyncio.create_task(restart())


@gcode("FIRMWARE_RESTART", "RESTART")
async def g_restart(cmd):
    asyncio.create_task(restart())


@gcode("M112")
async def g_estop(cmd):
    await shutdown("Shutdown due to emergency stop")


@gcode("STATUS")
async def g_status(cmd):
    await respond_info("Klipper state: %s" % S.KLIPPY["state"].capitalize())


@gcode("HELP")
async def g_help(cmd):
    await respond_info("Available extended commands:\n" + "\n".join(
        n for n in sorted(HANDLERS)))


def help_table():
    return {n: "" for n in sorted(HANDLERS)}
