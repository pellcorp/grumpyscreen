"""Testbench-only gcode, for driving a flow on demand (tools/sim_send.py):

    SIM_PRINT [FILE=x]          start a print (quick_cube.gcode), or cancel the running one
    SIM_SHUTDOWN [MSG=text]     klipper faults; SIM_READY (or FIRMWARE_RESTART) brings it back
    SIM_READY                   restart klipper
    SIM_PROMPT                  raise a klipper action:prompt dialog
    SIM_UPLOAD [FILE=x]         a new gcode file arrives, as an upload would
    SIM_DISCONNECT [SECONDS=5]  moonraker drops every client and refuses connections
    SIM_KLIPPER_STOP [SECONDS=10]  the klipper service stops, then starts again
    SIM_MESSAGE TEXT=.. [TYPE=warning|error]   queue an AFC message
    SIM_RUNOUT [LANE=x]         AFC: the lane runs dry (backup lane, or pause)
    SIM_FAULT [LANE=x|GATE=n]   AFC: toggle a lane fault; HH: jam a gate
    SIM_ERROR                   Happy Hare: toggle a pause_locked failure on gate 2
"""
import asyncio
import os

from . import state as S, files, klipper as K, moonraker as M, mmu
from .state import STATE
from .klipper import gcode


@gcode("SIM_PRINT")
async def sim_print(cmd):
    if K.SD.active() or STATE["print_stats"]["state"] == "paused":
        await K.g_cancel(cmd)
    else:
        await K.SD.print_file(cmd.get("FILE", "quick_cube.gcode"))


@gcode("SIM_SHUTDOWN")
async def sim_shutdown(cmd):
    await K.shutdown(cmd.get("MSG", "Heater extruder not heating at expected rate"))


@gcode("SIM_READY")
async def sim_ready(cmd):
    asyncio.create_task(K.restart())


@gcode("SIM_PROMPT")
async def sim_prompt(cmd):
    for line in ("// action:prompt_begin Filament runout",
                 "// action:prompt_text Lane 3 ran out mid-print. Load a new spool, then choose how to continue.",
                 "// action:prompt_button_group_start",
                 "// action:prompt_button Resume|RESUME|primary",
                 "// action:prompt_button Retry load|TOOL_LOAD LANE=lane3|secondary",
                 "// action:prompt_button_group_end",
                 "// action:prompt_footer_button Cancel print|CANCEL_PRINT|error",
                 "// action:prompt_show"):
        await S.console(line)


@gcode("SIM_UPLOAD")
async def sim_upload(cmd):
    src = cmd.get("FILE", "quick_cube.gcode")
    with open(os.path.join(S.ARGS.gcodes, src), "rb") as f:
        data = f.read()
    n = 1
    while os.path.exists(os.path.join(files.root(), "uploaded_%d.gcode" % n)):
        n += 1
    await files.add_file("uploaded_%d.gcode" % n, data)


@gcode("SIM_DISCONNECT")
async def sim_disconnect(cmd):
    asyncio.create_task(M.host_down(cmd.get_float("SECONDS", 5.0), "Moonraker connection dropped (sim)",
                                    klippy_restart=False))


@gcode("SIM_KLIPPER_STOP")
async def sim_klipper_stop(cmd):
    secs = cmd.get_float("SECONDS", 10.0)

    async def run():
        await M.service({"service": "klipper"}, "stop")
        await asyncio.sleep(secs)
        await M.service({"service": "klipper"}, "start")
    asyncio.create_task(run())


@gcode("SIM_MESSAGE")
async def sim_message(cmd):
    if mmu.BACKEND != "afc":
        await S.respond_info("SIM_MESSAGE needs --backend afc")
        return
    text = cmd.get("TEXT", "Test message")
    if cmd.get("TYPE", "warning") == "error":
        await mmu.afc_error(text)
    else:
        await mmu.afc_warning(text)
