"""Filament changers: AFC (lane objects, DEV-branch AFC_spool rules), Happy
Hare (one "mmu" object), or none (T<n> selects an extruder instead).

The commands follow the vendor's klipper modules; long operations block the
gcode queue for as long as they take, as they do on a printer.
"""
import asyncio
import re

from . import spoolman, klipper
from .state import STATE, GcodeError, console, respond_info, mark

BACKEND = "afc"
MSG_QUEUE = []          # AFC.message_queue: (text, type), head shown until cleared
HH_JAMMED = set()       # gates SIM_FAULT GATE=n has jammed (Happy Hare)
LOAD_TIME = 1.5         # seconds a load or unload takes

# 16 spool presets (material, colour, filled, map, weight). filled: True =
# fed to the hub, "staged" = on the prep sensor only (not loadable), False = empty
LANE_PRESETS = [
    ("PLA", "#F44336", True, "T0", 950), ("PETG", "#2196F3", True, "T1", 800),
    ("ABS", "#4CAF50", True, "T2", 1000), ("", "", False, "", 0),
    ("PLA", "#FFEB3B", "staged", "T0", 1000), ("PETG", "#212121", True, "T1", 750),
    ("ABS", "#FFFFFF", True, "", 1000), ("", "", False, "", 0),
    ("PLA", "#9C27B0", True, "T3", 600), ("PETG", "#00BCD4", True, "", 900),
    ("", "", False, "", 0), ("PLA", "#795548", True, "T2", 450),
    ("ASA", "#3F51B5", True, "", 1000), ("", "", False, "", 0),
    ("PLA", "#E91E63", True, "T0", 300), ("PETG", "#8BC34A", True, "", 1000),
]
HH_COLOR_NAMES = {"indigo": (0.294, 0.0, 0.510), "red": (1.0, 0.0, 0.0)}


def objects():
    """MMU state lives outside klippy's restart (AFC and HH save their vars)."""
    return [k for k in STATE if k == "AFC" or k.startswith("AFC_") or k == "mmu"]


def lane_key(name):
    return "AFC_lane " + name


# ------------------------------------------------------------------ AFC
def make_lane(name, index, tool, material, color, prep, load, weight):
    return {"name": name, "unit": "CANVAS_1", "hub": "toolhead_4way_hub", "extruder": "extruder",
            "buffer": "", "buffer_status": "", "lane": index, "map": [tool] if tool else [],
            "current_map": tool or "", "load": load, "prep": prep, "tool_loaded": False,
            "loaded_to_hub": False, "material": material, "spool_id": None, "color": color,
            "weight": weight, "extruder_temp": None, "bed_temp": None, "runout_lane": None,
            "filament_status": "Ready" if (prep and load) else "Not Ready",
            "filament_status_led": "ready", "status": "", "dist_hub": 1200}


def build_afc(a):
    count = max(1, min(a.lanes, len(LANE_PRESETS)))
    afc = {"current_load": None, "current_lane": None, "next_lane": None, "current_state": "Idle",
           "current_toolchange": 0, "number_of_toolchanges": 0, "spoolman": a.spoolman != "off",
           "td1_present": False, "lane_data_enabled": False, "error_state": False,
           "bypass_state": False, "quiet_mode": False, "position_saved": False,
           "units": ["Canvas CANVAS_1"], "lanes": [], "maps": [], "extruders": ["extruder"],
           "hubs": ["toolhead_4way_hub"], "buffers": [], "message": {"message": "", "type": ""},
           "led_state": True, "multiple_tool_mapping": bool(a.afc_multi_map)}
    STATE["AFC"] = afc
    taken = set()
    for i in range(count):
        mat, col, filled, tool, wt = LANE_PRESETS[i]
        if tool in taken and not a.afc_multi_map:
            tool = ""                 # without multiple mapping a T(n) lives on one lane
        name = "lane%d" % (i + 1)
        afc["lanes"].append(name)
        STATE[lane_key(name)] = make_lane(name, i, tool, mat, col, bool(filled), filled is True, wt)
        if tool:
            taken.add(tool)
    # AFC_functions.TcmdAssign: an unmapped lane gets the lowest free T(n)
    for name in afc["lanes"]:
        lane = STATE[lane_key(name)]
        if not lane["map"]:
            t = next("T%d" % x for x in range(99) if "T%d" % x not in taken)
            lane["map"], lane["current_map"] = [t], t
            taken.add(t)
    afc["maps"] = sorted(taken, key=lambda t: int(t[1:]))
    if STATE["AFC_lane lane1"]["load"]:
        afc["current_load"] = "lane1"
        STATE["AFC_lane lane1"].update(tool_loaded=True, loaded_to_hub=True)
    if count >= 5:
        STATE["AFC_lane lane1"]["runout_lane"] = "lane5"   # infinite spool demo
    for n in [x for x in a.spoolman_lanes.split(",") if x.strip()]:
        lane = STATE.get(lane_key("lane%s" % n.strip()))
        if lane is not None:
            lane["spool_id"] = 1000 + int(n)
    if a.message:
        MSG_QUEUE.append((a.message, a.message_type))
        afc["error_state"] = a.message_type == "error"
    refresh_message()


def refresh_message():
    afc = STATE["AFC"]
    afc["message"] = ({"message": MSG_QUEUE[0][0], "type": MSG_QUEUE[0][1]} if MSG_QUEUE
                      else {"message": "", "type": ""})


async def afc_error(text):
    await console("!! " + text)
    MSG_QUEUE.append((text.strip(), "error"))
    refresh_message()
    mark("AFC")


async def afc_warning(text):
    await console("<span class=warning--text>WARNING: %s</span>" % text)
    MSG_QUEUE.append((text, "warning"))
    refresh_message()
    mark("AFC")


def tool_lane(tool):
    return next((n for n in STATE["AFC"]["lanes"] if tool in STATE[lane_key(n)]["map"]), None)


async def afc_load(name, toolchange=False):
    afc = STATE["AFC"]
    lane = STATE.get(lane_key(name))
    if lane is None:
        raise GcodeError("%s Unknown" % name)
    if afc["current_load"] == name:
        await respond_info("%s already loaded" % name)
        return
    if not lane["load"]:
        # AFC.TOOL_LOAD: handle_lane_failure, pausing when in a print
        msg = ("Current lane not loaded, LOAD TRIGGER NOT TRIGGERED\n||==>--||----||-----||\n"
               "TRG   LOAD   HUB   TOOL\nPlease load lane before continuing.")
        printing = klipper.SD.active()
        if printing:
            msg += ("\nOnce issue is resolved please manually load %s with %s macro and click resume "
                    "to continue printing." % (name, lane["current_map"]))
        lane["status"] = "Error"
        afc["error_state"] = True
        mark(lane_key(name), "AFC")
        await afc_error(msg)
        if printing:
            await klipper.g_pause(klipper.Cmd("PAUSE"))
        return
    afc["current_state"] = "Tool Changing" if toolchange else "Loading"
    afc["current_lane"] = name
    mark("AFC")
    if afc["current_load"]:
        await afc_unload(during_change=True)
    await asyncio.sleep(LOAD_TIME)
    lane.update(prep=True, load=True, tool_loaded=True, loaded_to_hub=True)
    afc.update(current_load=name, current_lane=None, current_state="Idle")
    if toolchange:
        afc["current_toolchange"] += 1
    mark("AFC", lane_key(name))
    await respond_info("%s is now loaded in toolhead" % name)


async def afc_unload(during_change=False):
    afc = STATE["AFC"]
    if not afc["current_load"]:
        await respond_info("No lane is currently loaded")
        return
    name = afc["current_load"]
    afc["current_state"] = "Unloading"
    mark("AFC")
    await asyncio.sleep(LOAD_TIME)
    lane = STATE[lane_key(name)]
    lane["tool_loaded"] = False
    afc["current_load"] = None
    if not during_change:
        afc["current_state"] = "Idle"
    mark("AFC", lane_key(name))


async def afc_eject(name):
    lane = STATE.get(lane_key(name))
    if lane is None:
        raise GcodeError("%s Unknown" % name)
    afc = STATE["AFC"]
    if afc["current_load"] == name:
        await afc_error("Cannot eject %s, it is loaded in the toolhead" % name)
        return
    await asyncio.sleep(LOAD_TIME / 2)
    lane.update(prep=False, load=False, loaded_to_hub=False, tool_loaded=False,
                filament_status="Not Ready", color="", material="", weight=0, spool_id=None)
    mark(lane_key(name), "AFC")


async def afc_set_map(name, tool):
    afc = STATE["AFC"]
    cur = STATE.get(lane_key(name))
    holder = tool_lane(tool)
    if holder is None:
        await afc_error("Invalid map command: %s" % tool)
        return
    if cur is None:
        await respond_info("%s Unknown" % name)
        return
    sw = STATE[lane_key(holder)]
    if sw is cur:
        await respond_info("%s is already mapped to %s" % (tool, name))
        return
    if afc["multiple_tool_mapping"]:
        sw["map"] = [m for m in sw["map"] if m != tool]
        if sw["current_map"] == tool:
            sw["current_map"] = ""
        cur["map"] = [m for m in cur["map"] + [tool] if m != "NONE"]
    else:
        sw["map"] = list(cur["map"])
        sw["current_map"] = sw["map"][0] if sw["map"] else ""
        cur["map"], cur["current_map"] = [tool], tool
    mark(lane_key(name), lane_key(holder))


async def afc_runout(name):
    """A lane running dry: AFC hands over to its backup lane, or pauses."""
    lane = STATE.get(lane_key(name))
    afc = STATE["AFC"]
    if lane is None:
        return
    lane.update(prep=False, load=False, filament_status="Not Ready")
    mark(lane_key(name))
    backup = lane.get("runout_lane")
    if backup and STATE.get(lane_key(backup)):
        await afc_warning("%s empty, switching to %s" % (name, backup))
        lane["tool_loaded"] = False
        afc["current_load"] = None
        await afc_load(backup, toolchange=True)
        # AFC_SWAP_MAPPING FROM=<runout> TO=<backup>: the backup takes the tools
        b = STATE[lane_key(backup)]
        lane["map"], b["map"] = b["map"], lane["map"]
        lane["current_map"], b["current_map"] = b["current_map"], lane["current_map"]
        mark(lane_key(name), lane_key(backup))
    else:
        afc["error_state"] = True
        mark("AFC")
        await afc_error("%s ran out of filament and has no runout lane set" % name)
        await klipper.g_pause(klipper.Cmd("PAUSE"))


async def afc_fault(name):
    """AFC's handle_lane_failure: the lane goes to ERROR, the unit errors, the print pauses."""
    afc = STATE["AFC"]
    changed = {}
    if name:
        lane = STATE.get(lane_key(name))
        if not lane:
            return
        lane["status"] = "" if lane["status"] == "Error" else "Error"
        changed[lane_key(name)] = lane
        afc["error_state"] = lane["status"] == "Error"
    else:
        afc["error_state"] = not afc["error_state"]
    changed["AFC"] = afc
    mark(*changed)
    if afc["error_state"]:
        await klipper.g_pause(klipper.Cmd("PAUSE"))


async def handle_afc(cmd):
    afc = STATE["AFC"]
    p = cmd.params
    if cmd.name in ("TOOL_LOAD", "CHANGE_TOOL"):
        await afc_load(cmd.get("LANE"), toolchange=cmd.name == "CHANGE_TOOL")
    elif cmd.name == "TOOL_UNLOAD":
        await afc_unload()
    elif cmd.name == "LANE_UNLOAD":
        await afc_eject(cmd.get("LANE"))
    elif cmd.name == "SET_MATERIAL":
        lane = STATE[lane_key(cmd.get("LANE"))]
        lane["material"] = p.get("MATERIAL", "")
        mark(lane_key(lane["name"]))
    elif cmd.name == "SET_COLOR":
        lane = STATE[lane_key(cmd.get("LANE"))]
        lane["color"] = "#" + p.get("COLOR", "").lstrip("#")
        mark(lane_key(lane["name"]))
    elif cmd.name == "SET_SPOOL_ID":
        # like AFC: the lane takes the spool's colour, material and weight
        lane = STATE[lane_key(cmd.get("LANE"))]
        raw = str(p.get("SPOOL_ID", "")).strip()
        sid = int(raw) if raw.lstrip("-").isdigit() else -1  # empty clears
        s = spoolman.find(sid)
        lane["spool_id"] = sid if s else None
        if s:
            lane.update(color="#" + s["color"].lstrip("#"), material=s["material"],
                        weight=round(s["remaining"], 1))
        mark(lane_key(lane["name"]))
    elif cmd.name == "SET_WEIGHT":
        lane = STATE[lane_key(cmd.get("LANE"))]
        lane["weight"] = cmd.get_float("WEIGHT", 0.0)
        mark(lane_key(lane["name"]))
    elif cmd.name == "SET_RUNOUT":
        lane = STATE[lane_key(cmd.get("LANE"))]
        r = p.get("RUNOUT", "NONE")
        lane["runout_lane"] = None if r.upper() == "NONE" else r
        mark(lane_key(lane["name"]))
    elif cmd.name == "SET_MAP":
        await afc_set_map(cmd.get("LANE"), p.get("MAP", "").upper())
    elif cmd.name == "AFC_ENABLE_MULTIPLE_MAPPING":
        afc["multiple_tool_mapping"] = bool(cmd.get_int("ENABLE", 0))
        mark("AFC")
    elif cmd.name in ("AFC_BYPASS", "SET_BYPASS"):
        afc["bypass_state"] = not afc["bypass_state"]
        mark("AFC")
    elif cmd.name == "RESET_FAILURE":
        afc["error_state"] = False
        for n in afc["lanes"]:
            STATE[lane_key(n)]["status"] = ""
        mark("AFC", *(lane_key(n) for n in afc["lanes"]))
    elif cmd.name == "AFC_CLEAR_MESSAGE":
        if MSG_QUEUE:
            MSG_QUEUE.pop(0)
        refresh_message()
        mark("AFC")
    elif cmd.name == "_AFC_TEST_MESSAGES":
        for i in (1, 2, 3):
            await afc_error("Test Message %d" % i)
    elif cmd.name == "SIM_RUNOUT":
        await afc_runout(p.get("LANE", afc["current_load"] or ""))
    elif cmd.name == "SIM_FAULT":
        await afc_fault(p.get("LANE", ""))
    elif re.fullmatch(r"T\d+", cmd.name) and tool_lane(cmd.name):
        await afc_load(tool_lane(cmd.name), toolchange=True)
    else:
        return False
    return True


# ------------------------------------------------------------------ Happy Hare
def hh_rgb(color):
    if not color:
        return (0.0, 0.0, 0.0)
    if color in HH_COLOR_NAMES:
        return HH_COLOR_NAMES[color]
    try:
        h = color.lstrip("#")
        return tuple(round(int(h[i:i + 2], 16) / 255.0, 3) for i in (0, 2, 4))
    except (ValueError, IndexError):
        return (0.0, 0.0, 0.0)


def build_hh(a):
    count = max(1, min(a.lanes, len(LANE_PRESETS)))
    colors, materials, status, spool_ids, temps = [], [], [], [], []
    for i in range(count):
        mat, col, filled, _t, _w = LANE_PRESETS[i]
        materials.append(mat)
        colors.append(col.lstrip("#"))
        status.append(1 if filled else 0)
        spool_ids.append(i + 1 if filled else -1)
        temps.append({"PLA": 210, "PETG": 240, "ABS": 250, "ASA": 255}.get(mat, 210))
    if count >= 2:
        colors[1] = "indigo"          # a w3c name: exercises the name -> rgb path
    if count >= 6:
        status[5] = -1                # the unknown-gate state
    STATE["mmu"] = {
        "enabled": True, "num_gates": count, "gate": 0 if status[0] > 0 else -1,
        "tool": 0 if status[0] > 0 else -1, "unit": 0, "last_tool": -1, "next_tool": -1,
        "filament": "Loaded" if status[0] > 0 else "Unloaded", "filament_pos": 10,
        "action": "Idle", "print_state": "ready", "reason_for_pause": "",
        "gate_status": status, "gate_material": materials, "gate_color": colors,
        "gate_color_rgb": [hh_rgb(c) for c in colors], "gate_spool_id": spool_ids,
        "gate_temperature": temps, "ttg_map": list(range(count)),
        "endless_spool_groups": list(range(count)), "endless_spool_enabled": 1,
        "spoolman_support": a.spoolman, "has_bypass": True,
    }


async def hh_load(gate, tool):
    mmu = STATE["mmu"]
    if mmu["print_state"] in ("printing", "started"):
        mmu["next_tool"] = tool       # a pending next_tool is what says "swapping"
    mmu["action"] = "Loading"
    mark("mmu")
    await asyncio.sleep(LOAD_TIME)
    mmu["next_tool"] = -1
    if gate in HH_JAMMED:
        mmu.update(gate=gate, filament="Unloaded", action="Idle")
        mark("mmu")
        if mmu["print_state"] in ("printing", "started"):
            await hh_error("Filament stuck in gate %d" % gate)
        else:
            await console("!! MMU issue: Filament stuck in gate %d" % gate)
        return
    mmu.update(last_tool=mmu["tool"], gate=gate, tool=tool, filament="Loaded", action="Idle")
    mmu["gate_status"][gate] = 1
    mark("mmu")


async def hh_unload():
    mmu = STATE["mmu"]
    mmu["action"] = "Unloading"
    mark("mmu")
    await asyncio.sleep(LOAD_TIME)
    mmu["filament"] = "Unloaded"
    gate = mmu["gate"]
    if 0 <= gate < mmu["num_gates"] and mmu["gate_status"][gate] != 0:
        mmu["gate_status"][gate] = 2  # parked in the buffer, not back on the spool
    mmu["action"] = "Idle"
    mark("mmu")


async def hh_error(reason):
    """handle_mmu_error in a print: console lines, the error dialog, pause_locked."""
    mmu = STATE["mmu"]
    mmu.update(print_state="pause_locked", reason_for_pause=reason)
    await console("!! MMU issue detected. Print will be paused\nReason: %s" % reason)
    await console("// After fixing, call RESUME to continue printing (MMU_UNLOCK to restore temperature)")
    for line in ("// action:prompt_begin Happy Hare Error Notice",
                 "// action:prompt_text Print will be paused", "// action:prompt_text Reason: %s" % reason,
                 "// action:prompt_text After fixing, call RESUME to continue printing (MMU_UNLOCK to restore temperature)",
                 "// action:prompt_button_group_start", "// action:prompt_button UNLOCK|MMU_UNLOCK|secondary",
                 "// action:prompt_button RESUME|RESUME|warning", "// action:prompt_button_group_end",
                 "// action:prompt_show"):
        await console(line)
    mark("mmu")
    if klipper.SD.active():
        await klipper.g_pause(klipper.Cmd("PAUSE"))


async def handle_hh(cmd):
    mmu = STATE["mmu"]
    p = cmd.params
    if cmd.name == "MMU_CHANGE_TOOL" or re.fullmatch(r"T\d+", cmd.name):
        tool = int(p["TOOL"]) if "TOOL" in p else int(cmd.name[1:]) if cmd.name[0] == "T" else -1
        if not 0 <= tool < len(mmu["ttg_map"]):
            raise GcodeError("MMU: Invalid tool T%d" % tool)
        await hh_load(mmu["ttg_map"][tool], tool)
    elif cmd.name == "MMU_SELECT":
        mmu["gate"] = cmd.get_int("GATE")
        mark("mmu")
    elif cmd.name == "MMU_LOAD":
        gate = mmu["gate"]
        tools = [t for t, g in enumerate(mmu["ttg_map"]) if g == gate]
        await hh_load(gate, tools[0] if tools else -1)
    elif cmd.name == "MMU_UNLOAD":
        await hh_unload()
    elif cmd.name == "MMU_EJECT":
        mmu["gate_status"][cmd.get_int("GATE")] = 0
        mark("mmu")
    elif cmd.name in ("MMU_TTG_MAP", "MMU_REMAP_TTG") and "TOOL" in p and "GATE" in p:
        mmu["ttg_map"][cmd.get_int("TOOL")] = cmd.get_int("GATE")
        mark("mmu")
    elif cmd.name == "MMU_GATE_MAP" and "GATE" in p:
        g = cmd.get_int("GATE")
        if "MATERIAL" in p:
            mmu["gate_material"][g] = p["MATERIAL"]
        if "COLOR" in p:
            mmu["gate_color"][g] = p["COLOR"]
            mmu["gate_color_rgb"][g] = hh_rgb(p["COLOR"])
        if "SPOOLID" in p:
            mmu["gate_spool_id"][g] = cmd.get_int("SPOOLID")
        if "TEMP" in p:
            mmu["gate_temperature"][g] = cmd.get_int("TEMP")
        mark("mmu")
    elif cmd.name == "MMU_SPOOLMAN" and ("GATE" in p or "SPOOLID" in p):
        # pull mode's gate map edit: a spool sits in one gate at most, and
        # GATE or SPOOLID alone unsets
        sid = cmd.get_int("SPOOLID") if "SPOOLID" in p else -1
        if sid > 0:
            mmu["gate_spool_id"] = [-1 if x == sid else x for x in mmu["gate_spool_id"]]
        if "GATE" in p:
            mmu["gate_spool_id"][cmd.get_int("GATE")] = sid
        mark("mmu")
    elif cmd.name == "MMU_ENDLESS_SPOOL":
        if "GROUPS" in p:
            mmu["endless_spool_groups"] = [int(x) for x in p["GROUPS"].split(",")]
        if "ENABLE" in p:
            mmu["endless_spool_enabled"] = cmd.get_int("ENABLE")
        mark("mmu")
    elif cmd.name == "MMU_UNLOCK":
        if mmu["print_state"] == "pause_locked":
            mmu["print_state"] = "paused"
        await console("// action:prompt_end")
        mark("mmu")
    elif cmd.name == "MMU_RECOVER":
        await respond_info("Recovered filament position")
    elif cmd.name == "SIM_FAULT":
        gate = int(p.get("GATE", 2))
        HH_JAMMED.symmetric_difference_update({gate})
    elif cmd.name == "SIM_ERROR":
        if mmu["print_state"] == "pause_locked":
            mmu.update(print_state="printing", reason_for_pause="")
            mark("mmu")
        else:
            mmu["gate"] = 2
            await hh_error("Filament stuck in gate 2")
    else:
        return False
    return True


# ------------------------------------------------------------------ hooks
def build(a):
    global BACKEND
    BACKEND = a.backend
    MSG_QUEUE.clear()
    if BACKEND == "afc":
        build_afc(a)
    elif BACKEND == "hh":
        build_hh(a)
    rows = []
    if BACKEND == "hh":
        m = STATE["mmu"]
        rows = [(sid, "Spool %d" % sid, mat or "PLA",
                 "%02X%02X%02X" % tuple(int(c * 255) for c in hh_rgb(col)), "Polymaker")
                for sid, mat, col in zip(m["gate_spool_id"], m["gate_material"], m["gate_color"]) if sid > 0]
        # spares to put in a gate, numbered after the gate spools
        top = max([r[0] for r in rows] + [0])
        rows += [(top + i, n, mat, c, v) for i, (n, mat, c, v) in enumerate(spoolman.CATALOGUE, 1)]
    elif BACKEND == "afc":
        rows = [(STATE[lane_key(n)]["spool_id"], "Lane %s spool" % n, STATE[lane_key(n)]["material"] or "PLA",
                 STATE[lane_key(n)]["color"].lstrip("#") or "FFFFFF", "Polymaker")
                for n in STATE["AFC"]["lanes"] if STATE[lane_key(n)]["spool_id"]]
        if rows:
            rows = [(i, n, m, c, v) for i, (n, m, c, v) in enumerate(spoolman.CATALOGUE, 1)] + rows
    spoolman.build(rows)


async def handle(cmd):
    """Gcode the printer did not claim; True when the changer took it."""
    if BACKEND == "afc":
        return await handle_afc(cmd)
    if BACKEND == "hh":
        return await handle_hh(cmd)
    if cmd.name in ("SIM_RUNOUT", "SIM_FAULT", "SIM_ERROR"):
        await respond_info("%s needs an MMU backend (--backend afc|hh)" % cmd.name)
        return True
    return False


async def tool(n):
    """A T<n> nobody registered: with several extruders it selects one."""
    name = "extruder" if n == 0 else "extruder%d" % n
    if BACKEND == "none" and name in STATE:
        await klipper.g_activate(klipper.Cmd("ACTIVATE_EXTRUDER EXTRUDER=%s" % name))
    else:
        await respond_info('Unknown command:"T%d"' % n)


def current_spool():
    if BACKEND == "hh":
        m = STATE["mmu"]
        return m["gate_spool_id"][m["gate"]] if 0 <= m["gate"] < len(m["gate_spool_id"]) else None
    if BACKEND == "afc" and STATE["AFC"]["current_load"]:
        sid = STATE[lane_key(STATE["AFC"]["current_load"])]["spool_id"]
        if sid:
            return sid
    return spoolman.ACTIVE["id"]


BURN = {"mm": 0.0}


def extruded(mm):
    """Filament leaves the loaded spool: spoolman's count, and the lane's weight."""
    spoolman.use(current_spool(), mm)
    if BACKEND == "afc" and STATE["AFC"]["current_load"]:
        lane = STATE[lane_key(STATE["AFC"]["current_load"])]
        if lane["weight"]:
            BURN["mm"] += mm
            if BURN["mm"] > 30:       # report whole grams, like AFC's spool tracking
                lane["weight"] = max(0, round(lane["weight"] - BURN["mm"] * spoolman.GRAMS_PER_MM, 1))
                BURN["mm"] = 0.0
                mark(lane_key(lane["name"]))
                if lane["weight"] <= 0:
                    asyncio.create_task(afc_runout(lane["name"]))


async def print_event(state):
    """Happy Hare keeps its own job state alongside klipper's print_stats."""
    if BACKEND != "hh":
        return
    mmu = STATE["mmu"]
    if state == "printing":
        if mmu["print_state"] not in ("pause_locked", "paused", "printing"):
            mmu["print_state"] = "started"
            mark("mmu")
            await asyncio.sleep(0.5)
        mmu["print_state"] = "printing"
    elif state == "resumed":
        mmu.update(print_state="printing", reason_for_pause="")
    elif state == "paused":
        if mmu["print_state"] != "pause_locked":
            mmu["print_state"] = "paused"
    elif state in ("complete", "cancelled", "error"):
        mmu.update(print_state=state, reason_for_pause="")
    mark("mmu")


async def on_resume():
    """AFC wraps RESUME and clears its error state on the way."""
    if BACKEND == "afc" and STATE["AFC"]["error_state"]:
        STATE["AFC"]["error_state"] = False
        mark("AFC")
