"""Shared state and the outbound side of the protocol.

STATE holds klipper's printer objects exactly as get_status would report
them. Whatever changes an object calls mark(name); status_tick() then sends
the changed fields on klipper's 250 ms subscription tick.
"""
import asyncio
import collections
import json
import time

ARGS = None            # argparse namespace, set once by fake_moonraker.py
STATE = {}             # klipper printer objects
CLIENTS = {}           # websocket -> {"subs": {obj: [fields] | None}, "id": int, "info": {}}
GCODE_STORE = collections.deque(maxlen=1000)
KLIPPY = {"state": "startup", "message": "Klipper is starting"}
HOST = {"up": True}    # False while the simulated host is rebooting/shut down
START = time.time()


class GcodeError(Exception):
    """A klipper command error: '!! text' on the console, error 400 to the caller."""

    def __init__(self, message):
        super().__init__(message)
        self.message = message


class RpcError(Exception):
    """A moonraker json-rpc error reply."""

    def __init__(self, message, code=400):
        super().__init__(message)
        self.code, self.message = code, message


def eventtime():
    return round(time.monotonic(), 3)


async def send(ws, msg):
    try:
        await ws.send(msg)
    except Exception:
        pass


async def notify(method, *params):
    """A moonraker server-to-client notification, to every connected client."""
    msg = json.dumps({"jsonrpc": "2.0", "method": method, "params": list(params)})
    for ws in list(CLIENTS):
        await send(ws, msg)


def filtered(status, subs):
    out = {}
    for obj, data in status.items():
        if obj not in subs:
            continue
        fields = subs[obj]
        out[obj] = dict(data) if fields is None else {k: v for k, v in data.items() if k in fields}
        if not out[obj]:
            del out[obj]
    return out


LAST = {}              # what klipper last reported per object, to diff against


def changed_fields(diff):
    """Klipper's subscriptions only carry fields whose value changed; the UI
    relies on it (a print_stats.filename in an update means a new print)."""
    out = {}
    for obj, data in diff.items():
        last = LAST.setdefault(obj, {})
        fresh = {}
        for k, v in data.items():
            enc = json.dumps(v, sort_keys=True)
            if last.get(k) != enc:
                last[k] = enc
                fresh[k] = json.loads(enc)
        if fresh:
            out[obj] = fresh
    return out


DIRTY = set()          # objects with news for the next status tick


def mark(*objs):
    """Queue objects for the next notify_status_update. Klipper reports
    subscriptions on a 250 ms tick, so everything that changed in one tick --
    a new print's filename and its first heater targets -- arrives together."""
    DIRTY.update(objs)


async def status_tick():
    while True:
        await asyncio.sleep(0.25)
        if not DIRTY:
            continue
        objs = list(DIRTY)
        DIRTY.clear()
        diff = changed_fields({o: STATE[o] for o in objs if o in STATE})
        if not diff:
            continue
        et = eventtime()
        for ws, c in list(CLIENTS.items()):
            part = filtered(diff, c["subs"])
            if part:
                await send(ws, json.dumps({"jsonrpc": "2.0", "method": "notify_status_update",
                                           "params": [part, et]}))


def snapshot(objects):
    """printer.objects.query/subscribe: the requested objects and fields."""
    out = {}
    for obj, fields in objects.items():
        if obj not in STATE:
            continue
        data = STATE[obj]
        out[obj] = dict(data) if not fields else {k: data[k] for k in fields if k in data}
    return out


def record_command(script):
    """Moonraker stores the commands its clients send, but klipper never echoes them."""
    GCODE_STORE.append({"message": script, "time": time.time(), "type": "command"})


async def console(line):
    """A line of klipper output, as notify_gcode_response and in the gcode store."""
    GCODE_STORE.append({"message": line, "time": time.time(), "type": "response"})
    await notify("notify_gcode_response", line)


async def respond_info(text):
    for line in text.split("\n"):
        await console("// " + line)
