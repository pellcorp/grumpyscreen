#!/usr/bin/env python3
"""Enough of Moonraker to run the screen against, without a printer.

It answers the calls the init panel makes with an idle printer (extruder, bed,
toolhead, print_stats and friends), so the panels come up, and implements
machine.update.client the way Moonraker's update_manager does for the COSMOS
updater, which is the part that otherwise needs a COSMOS printer to try.

Usage:

    python3 tools/fake_moonraker.py --update ok

Then point the screen at it in grumpyscreen.cfg:

    [moonraker]
    host: 127.0.0.1
    port: 7125

    --update none     nothing newer to install: the request is answered "ok"
                      and no progress is sent (Moonraker's behaviour when the
                      updater reports no update available)
    --update ok       progress messages, a final message marked complete, then
                      "ok", like a real COSMOS update up to the reboot
    --update fail     progress, then "Error updating cosmos: ..." marked
                      complete, then a JSON-RPC error, as Moonraker does when
                      an update raises
    --update refuse   a JSON-RPC error straight away and no progress, as when
                      Klipper is printing
    --update none,ok  a comma separated list is used in turn, one mode per
                      update request, so every case can be tried in one run
    --port N          listen on another port (default 7125)

Everything is standard library: a small RFC 6455 server, one thread per
client. Unknown methods get Moonraker's "Method not found" error. Every request
is logged to stdout.
"""

import argparse, base64, hashlib, json, socket, struct, sys, threading, time

APP = "cosmos"

OBJECTS = ["webhooks", "configfile", "toolhead", "extruder", "heater_bed",
           "print_stats", "virtual_sdcard", "display_status", "idle_timeout",
           "gcode_move", "motion_report", "fan", "system_stats"]

STATUS = {
    "webhooks": {"state": "ready", "state_message": "Printer is ready"},
    "configfile": {"config": {}, "settings": {}},
    "toolhead": {"homed_axes": "", "position": [0.0, 0.0, 0.0, 0.0],
                 "print_time": 0.0, "estimated_print_time": 0.0,
                 "max_velocity": 500.0, "max_accel": 5000.0,
                 "axis_minimum": [0.0, 0.0, 0.0, 0.0],
                 "axis_maximum": [256.0, 256.0, 256.0, 0.0],
                 "extruder": "extruder"},
    "extruder": {"temperature": 24.0, "target": 0.0, "power": 0.0,
                 "pressure_advance": 0.04, "smooth_time": 0.04,
                 "can_extrude": False},
    "heater_bed": {"temperature": 23.0, "target": 0.0, "power": 0.0},
    "print_stats": {"state": "standby", "filename": "", "total_duration": 0.0,
                    "print_duration": 0.0, "filament_used": 0.0,
                    "message": "", "info": {"total_layer": None,
                                            "current_layer": None}},
    "virtual_sdcard": {"progress": 0.0, "is_active": False,
                       "file_position": 0},
    "display_status": {"progress": 0.0, "message": None},
    "idle_timeout": {"state": "Idle", "printing_time": 0.0},
    "gcode_move": {"speed_factor": 1.0, "extrude_factor": 1.0,
                   "speed": 1500.0, "absolute_coordinates": True,
                   "absolute_extrude": False,
                   "homing_origin": [0.0, 0.0, 0.0, 0.0],
                   "gcode_position": [0.0, 0.0, 0.0, 0.0]},
    "motion_report": {"live_position": [0.0, 0.0, 0.0, 0.0],
                      "live_velocity": 0.0, "live_extruder_velocity": 0.0},
    "fan": {"speed": 0.0, "rpm": None},
    "system_stats": {"sysload": 0.1, "cputime": 1.0, "memavail": 60000},
}

PRINTER_INFO = {"state": "ready", "state_message": "Printer is ready",
                "hostname": "fake-moonraker", "software_version": "fake",
                "klipper_path": "/tmp", "python_path": "/usr/bin/python3",
                "log_file": "/tmp/klippy.log", "config_file": "/tmp/printer.cfg"}

SERVER_INFO = {"klippy_connected": True, "klippy_state": "ready",
               "components": ["update_manager", "machine", "file_manager"],
               "failed_components": [], "registered_directories": ["gcodes"],
               "warnings": [], "websocket_count": 1,
               "moonraker_version": "fake", "api_version": [1, 5, 0],
               "api_version_string": "1.5.0"}

# What COSMOS' SWUpdate backend reports, in order, while it updates.
PROGRESS = ["SWUpdate cosmos: Updating...",
            "SWUpdate cosmos: Downloading Release...",
            "SWUpdate cosmos: Download Complete, installing firmware...",
            "SWUpdate cosmos: Installed version 26.10.0, rebooting..."]


class Modes:
    """The update modes to answer with, in turn, shared by every client."""
    def __init__(self, modes):
        self.modes, self.i, self.lock = modes, 0, threading.Lock()

    def next(self):
        with self.lock:
            mode = self.modes[self.i % len(self.modes)]
            self.i += 1
            return mode


class Client:
    def __init__(self, conn, addr, modes):
        self.conn, self.addr, self.modes = conn, addr, modes
        self.send_lock = threading.Lock()
        self.proc_id = 0

    # --- websocket framing ---------------------------------------------
    def handshake(self):
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = self.conn.recv(4096)
            if not chunk:
                return False
            data += chunk
        headers = {}
        for line in data.decode(errors="replace").split("\r\n")[1:]:
            if ":" in line:
                k, v = line.split(":", 1)
                headers[k.strip().lower()] = v.strip()
        key = headers.get("sec-websocket-key")
        if key is None:
            self.conn.sendall(b"HTTP/1.1 400 Bad Request\r\n\r\n")
            return False
        accept = base64.b64encode(hashlib.sha1(
            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        self.conn.sendall(b"HTTP/1.1 101 Switching Protocols\r\n"
                          b"Upgrade: websocket\r\nConnection: Upgrade\r\n"
                          b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n")
        return True

    def recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.conn.recv(n - len(buf))
            if not chunk:
                raise ConnectionError
            buf += chunk
        return buf

    def recv_message(self):
        message, opcode = b"", None
        while True:
            b1, b2 = self.recv_exact(2)
            fin, op = b1 & 0x80, b1 & 0x0F
            length = b2 & 0x7F
            if length == 126:
                length = struct.unpack(">H", self.recv_exact(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self.recv_exact(8))[0]
            mask = self.recv_exact(4) if b2 & 0x80 else b"\0\0\0\0"
            payload = bytes(c ^ mask[i % 4]
                            for i, c in enumerate(self.recv_exact(length)))
            if op == 0x8:
                raise ConnectionError
            if op == 0x9:
                self.send_frame(0xA, payload)
                continue
            if op == 0xA:
                continue
            if op != 0x0:
                opcode = op
            message += payload
            if fin:
                return opcode, message

    def send_frame(self, opcode, payload):
        header = bytes([0x80 | opcode])
        n = len(payload)
        if n < 126:
            header += bytes([n])
        elif n < 65536:
            header += bytes([126]) + struct.pack(">H", n)
        else:
            header += bytes([127]) + struct.pack(">Q", n)
        with self.send_lock:
            self.conn.sendall(header + payload)

    def send(self, obj):
        self.send_frame(0x1, json.dumps(obj).encode())

    # --- json-rpc ------------------------------------------------------
    def reply(self, rid, result):
        self.send({"jsonrpc": "2.0", "result": result, "id": rid})

    def error(self, rid, code, message):
        self.send({"jsonrpc": "2.0", "error": {"code": code,
                                               "message": message}, "id": rid})

    def notify_update(self, message, complete=False):
        print(f"  -> notify_update_response {message!r} complete={complete}")
        self.send({"jsonrpc": "2.0", "method": "notify_update_response",
                   "params": [{"message": message, "application": APP,
                               "proc_id": self.proc_id,
                               "complete": complete}]})

    def update(self, rid, params):
        name = params.get("name")
        if name != APP:
            self.error(rid, 404, f"Updater {name} not available")
            return
        mode = self.modes.next()
        print(f"  update mode: {mode}")
        self.proc_id += 1
        if mode == "refuse":
            self.error(rid, 503, "Update Refused: Klippy is printing")
        elif mode == "none":
            self.reply(rid, "ok")
        elif mode == "ok":
            for msg in PROGRESS:
                self.notify_update(msg)
                time.sleep(1.5)
            self.notify_update("SWUpdate cosmos: Update Finished...", True)
            self.reply(rid, "ok")
        elif mode == "fail":
            for msg in PROGRESS[:2]:
                self.notify_update(msg)
                time.sleep(1.5)
            err = "SWUpdate cosmos: Release asset 'cosmos-centauri-carbon-1.swu' not found"
            self.notify_update(f"Error updating {APP}: {err}", True)
            self.error(rid, 500, err)

    def handle(self, req):
        method, rid = req.get("method"), req.get("id")
        params = req.get("params") or {}
        print(f"{self.addr[0]} {method} {json.dumps(params)[:120]}")
        if rid is None:
            return
        if method == "server.info":
            self.reply(rid, SERVER_INFO)
        elif method == "printer.info":
            self.reply(rid, PRINTER_INFO)
        elif method == "printer.objects.list":
            self.reply(rid, {"objects": OBJECTS})
        elif method in ("printer.objects.subscribe", "printer.objects.query"):
            wanted = params.get("objects", {}) or {}
            status = {k: v for k, v in STATUS.items() if k in wanted}
            self.reply(rid, {"eventtime": time.monotonic(), "status": status})
        elif method == "server.files.roots":
            self.reply(rid, [{"name": "gcodes", "path": "/tmp/gcodes",
                              "permissions": "rw"}])
        elif method == "server.files.list":
            self.reply(rid, [])
        elif method in ("printer.gcode.script", "printer.emergency_stop",
                        "printer.firmware_restart"):
            self.reply(rid, "ok")
        elif method == "machine.update.client":
            # off the reader thread, the way Moonraker answers only once the
            # update request is over
            threading.Thread(target=self.update, args=(rid, params),
                             daemon=True).start()
        else:
            self.error(rid, -32601, "Method not found")

    def run(self):
        try:
            if not self.handshake():
                return
            print(f"{self.addr[0]} connected")
            while True:
                opcode, message = self.recv_message()
                if opcode != 0x1:
                    continue
                try:
                    req = json.loads(message)
                except ValueError:
                    continue
                self.handle(req)
        except (ConnectionError, OSError):
            pass
        finally:
            print(f"{self.addr[0]} disconnected")
            self.conn.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", type=int, default=7125)
    ap.add_argument("--update", default="ok",
                    help="none, ok, fail or refuse, or a comma separated list")
    args = ap.parse_args()
    modes = [m.strip() for m in args.update.split(",") if m.strip()]
    bad = [m for m in modes if m not in ("none", "ok", "fail", "refuse")]
    if not modes or bad:
        ap.error(f"unknown update mode: {', '.join(bad) or args.update}")
    modes = Modes(modes)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.port))
    srv.listen()
    print(f"fake moonraker on port {args.port}, update mode: {args.update}")
    try:
        while True:
            conn, addr = srv.accept()
            threading.Thread(target=Client(conn, addr, modes).run,
                             daemon=True).start()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    sys.exit(main())
