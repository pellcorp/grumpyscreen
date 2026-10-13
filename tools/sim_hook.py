#!/usr/bin/env python3
"""Stand-ins for grumpyscreen.cfg's [commands] while the simulator runs.

fake_moonraker.py writes these into the session's UI config instead of the device
scripts, so the settings panel's buttons act on the simulation and never on
the machine running it:

    restart-klipper   machine.services.restart klipper
    restart-gui       the UI exits and sim.sh starts it again
    shutdown          machine.shutdown: the sim host goes down, then comes back,
                      and the UI restarts with it like a power cycle
    switch-to-stock   nothing to switch to: logs it and reboots the sim host
    factory-reset     session files, history, spools, wifi and UI settings
                      back to their starting state, then the UI restarts
    support-zip       a zip of the session logs, in the session directory
    restart-wifi      the fake wpa_supplicant drops and rejoins

    python3 tools/sim_hook.py <action> [--port 7125]
"""
import argparse
import asyncio
import os
import subprocess
import socket
import sys
import time
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from moonsim.ws import call as rpc  # noqa: E402


def wpa(request):
    d = os.environ.get("WPA_SUPPLICANT_SOCKET")
    if not d or not os.path.isdir(d):
        return
    me = "/tmp/sim_hook_%d" % os.getpid()
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    s.bind(me)
    try:
        for name in os.listdir(d):
            s.sendto(request.encode(), os.path.join(d, name))
            s.settimeout(2)
            s.recv(256)
    finally:
        s.close()
        os.unlink(me)


def restart_gui(session):
    """sim.sh relaunches the UI when it finds this flag after the UI exits."""
    open(os.path.join(session, "restart-gui"), "w").close()
    # let the UI's call return first, as the device's init script does
    subprocess.Popen(["sh", "-c", "sleep 0.5; kill -TERM %d" % os.getppid()], start_new_session=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action")
    ap.add_argument("--port", type=int, default=7125)
    a = ap.parse_args()
    session = "/tmp/grumpy-sim-%d" % a.port
    print("[sim_hook] %s" % a.action, flush=True)
    if a.action == "restart-klipper":
        r = asyncio.run(rpc(a.port, "machine.services.restart", {"service": "klipper"}))
    elif a.action in ("shutdown", "switch-to-stock"):
        if a.action == "switch-to-stock":
            print("[sim_hook] the sim has no stock firmware; rebooting the sim host instead")
        r = asyncio.run(rpc(a.port, "machine.shutdown" if a.action == "shutdown" else "machine.reboot"))
        # the screen goes down with the host and boots again with it
        restart_gui(session)
    elif a.action == "factory-reset":
        r = asyncio.run(rpc(a.port, "sim.reset"))
        wpa("SIM_RESET")
        override = os.environ.get("CONFIG_OVERRIDE_FILE")
        if override and override.startswith(session) and os.path.exists(override):
            os.remove(override)
        restart_gui(session)
    elif a.action == "support-zip":
        out = os.path.join(session, "support-%s.zip" % time.strftime("%Y%m%d-%H%M%S"))
        with zipfile.ZipFile(out, "w") as z:
            for name in os.listdir(session):
                if name.endswith((".log", ".cfg")):
                    z.write(os.path.join(session, name), name)
        print("[sim_hook] wrote %s" % out)
        return 0
    elif a.action == "restart-wifi":
        wpa("SIM_RESTART")
        time.sleep(1)
        return 0
    elif a.action == "restart-gui":
        restart_gui(session)
        return 0
    else:
        print("[sim_hook] unknown action %s" % a.action)
        return 1
    return 0 if "result" in r else 1


if __name__ == "__main__":
    sys.exit(main())
