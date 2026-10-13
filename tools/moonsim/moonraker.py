"""Moonraker's JSON-RPC surface: printer.*, server.*, machine.*.

Result shapes, notifications and refusal texts follow moonraker's source
(components/*.py at 9e676eba). Things that would touch a real machine --
reboot, shutdown, service restarts, updates, power devices -- act on the
simulated printer only.
"""
import asyncio
import math
import os
import random
import time

from . import state as S, files, klipper as K, mmu, spoolman
from .state import STATE, RpcError, notify, console

GCODE_LOCK = asyncio.Lock()     # klipper's gcode mutex


# ------------------------------------------------------------------ gcode entry
async def run_gcode(script):
    """printer.gcode.script: commands run in order, holding the gcode mutex."""
    for line in script.split("\n"):
        if K.strip_comment(line):
            S.record_command(line.strip())
    async with GCODE_LOCK:
        for line in script.split("\n"):
            await K.run_line(line)
    return "ok"


async def sd_line(line):
    """virtual_sdcard's dispatch, taking the mutex per line like klippy."""
    K.SD.lock_wait = True
    async with GCODE_LOCK:
        K.SD.lock_wait = False
        if K.SD.must_pause:
            return None
        K.SD.from_sd = True
        try:
            return await K.run_line(line, from_sd=True)
        finally:
            K.SD.from_sd = False


# ------------------------------------------------------------------ klippy lifecycle
async def klippy_state(state):
    if state == "ready":
        await notify("notify_klippy_ready")
    elif state == "shutdown":
        await notify("notify_klippy_shutdown")
        await HISTORY.finish_current("klippy_shutdown")
    elif state == "disconnected":
        await notify("notify_klippy_disconnected")
        await HISTORY.finish_current("klippy_disconnect")
    if state in ("ready", "shutdown"):
        await console("// Klipper state: %s" % state.capitalize())


def require_klippy():
    if S.KLIPPY["state"] == "disconnected":
        raise RpcError("Klippy Host not connected", 503)


async def print_event(state):
    """virtual_sdcard's job events, as moonraker's job_state sees them."""
    if state == "started":
        await HISTORY.start(STATE["print_stats"]["filename"])
    elif state in ("complete", "cancelled", "error"):
        await HISTORY.finish_current({"complete": "completed"}.get(state, state))
        asyncio.create_task(QUEUE.print_done(state))
    await mmu.print_event(state)


# ------------------------------------------------------------------ history
class History:
    def __init__(self):
        self.jobs = []
        self.next_id = 1
        self.current = None
        self.totals = {"total_jobs": 0, "total_time": 0.0, "total_print_time": 0.0,
                       "total_filament_used": 0.0, "longest_job": 0.0, "longest_print": 0.0}

    def public(self, job):
        rel = job["filename"]
        meta = files.METADATA.get(rel)
        exists = meta is not None and abs(meta["modified"] - job["metadata"].get("modified", -1)) < 0.01
        return dict(job, exists=exists)

    def seed(self, count):
        lib = [r for r in files.walk() if "error" not in r][:max(0, count)]
        t = time.time() - 86400 * 3
        statuses = ["completed", "completed", "cancelled", "completed", "error", "completed"]
        for i, rel in enumerate(lib):
            meta = files.METADATA[rel]
            dur = float(meta.get("estimated_time", 600))
            status = statuses[i % len(statuses)]
            frac = 1.0 if status == "completed" else 0.4
            job = self.new(rel, t)
            job.update(status=status, end_time=t + dur * frac + 120, total_duration=dur * frac + 120,
                       print_duration=dur * frac, filament_used=meta.get("filament_total", 0) * frac)
            self.add_totals(job)
            t += 3600 * 5

    def new(self, rel, start):
        meta = {k: v for k, v in files.METADATA.get(rel, {}).items() if k not in ("print_start_time", "job_id")}
        job = {"job_id": "%06X" % self.next_id, "user": "No User", "filename": rel, "status": "in_progress",
               "start_time": start, "end_time": None, "print_duration": 0.0, "total_duration": 0.0,
               "filament_used": 0.0, "metadata": meta, "auxiliary_data": []}
        self.next_id += 1
        self.jobs.append(job)
        return job

    def add_totals(self, job):
        t = self.totals
        t["total_jobs"] += 1
        t["total_time"] = round(t["total_time"] + job["total_duration"], 2)
        t["total_print_time"] = round(t["total_print_time"] + job["print_duration"], 2)
        t["total_filament_used"] = round(t["total_filament_used"] + job["filament_used"], 2)
        t["longest_job"] = max(t["longest_job"], job["total_duration"])
        t["longest_print"] = max(t["longest_print"], job["print_duration"])

    async def start(self, rel):
        if self.current:
            await self.finish_current("cancelled")
        job = self.new(rel, time.time())
        self.current = job
        if rel in files.METADATA:
            files.METADATA[rel].update(print_start_time=job["start_time"], job_id=job["job_id"])
        await notify("notify_history_changed", {"action": "added", "job": self.public(job)})

    async def finish_current(self, status):
        job = self.current
        if not job:
            return
        ps = STATE.get("print_stats", {})
        job.update(status=status, end_time=time.time(),
                   print_duration=ps.get("print_duration", 0.0), total_duration=ps.get("total_duration", 0.0),
                   filament_used=round(ps.get("filament_used", 0.0), 2))
        self.add_totals(job)
        self.current = None
        await notify("notify_history_changed", {"action": "finished", "job": self.public(job)})


HISTORY = History()


# ------------------------------------------------------------------ job queue
class JobQueue:
    def __init__(self):
        self.jobs = []
        self.state = "paused"

    def result(self):
        now = time.time()
        return {"queued_jobs": [dict(j, time_in_queue=round(now - j["time_added"], 2)) for j in self.jobs],
                "queue_state": self.state}

    async def changed(self, action):
        r = self.result()
        await asyncio.sleep(0.05)
        await notify("notify_job_queue_changed", {"action": action,
                                                  "updated_queue": None if action == "state_changed" else r["queued_jobs"],
                                                  "queue_state": self.state})

    async def post(self, names, reset=False):
        if isinstance(names, str):
            names = [n.strip() for n in names.split(",") if n.strip()]
        for n in names:
            if n not in files.METADATA:
                raise RpcError("G-Code File %s does not exist" % n)
        if reset:
            self.jobs.clear()
        for n in names:
            self.jobs.append({"filename": n, "job_id": "%016X" % random.getrandbits(48),
                              "time_added": time.time()})
        if STATE["print_stats"]["state"] in ("printing", "paused"):
            self.state = "ready"
        asyncio.create_task(self.changed("jobs_added"))
        return self.result()

    async def start(self):
        if not self.jobs:
            self.state = "paused"
        elif STATE["print_stats"]["state"] in ("printing", "paused"):
            self.state = "ready"
        else:
            await self.load_next()
        asyncio.create_task(self.changed("state_changed"))
        return self.result()

    async def load_next(self):
        if not self.jobs:
            self.state = "paused"
            return
        self.state = "loading"
        job = self.jobs.pop(0)
        await run_gcode('SDCARD_PRINT_FILE FILENAME="%s"' % job["filename"])
        self.state = "ready" if self.jobs else "paused"
        asyncio.create_task(self.changed("job_loaded"))

    async def print_done(self, status):
        if status == "complete" and self.state == "ready" and self.jobs:
            await asyncio.sleep(2)
            await self.load_next()


QUEUE = JobQueue()


# ------------------------------------------------------------------ power devices
POWER = {}


def build_power():
    POWER.clear()
    if not S.ARGS.no_power_devices:
        POWER["printer"] = {"device": "printer", "status": "on", "locked_while_printing": True, "type": "gpio"}
        POWER["chamber_light"] = {"device": "chamber_light", "status": "off",
                                  "locked_while_printing": False, "type": "tplink_smartplug"}


async def set_power(name, action):
    dev = POWER.get(name)
    if dev is None:
        raise RpcError("No valid device named %s" % name)
    action = action.lower()
    if action not in ("on", "off", "toggle"):
        raise RpcError("Invalid requested action '%s'" % action)
    want = ("off" if dev["status"] == "on" else "on") if action == "toggle" else action
    if want == dev["status"]:
        return dev["status"]
    if dev["locked_while_printing"] and is_printing():
        raise RpcError("Unable to change power for %s while printing" % name)
    dev["status"] = want
    await notify("notify_power_changed", dict(dev))
    if name == "printer":
        # bound to the klipper service: power off stops it, power on starts it
        if want == "off":
            await service_state("klipper", "inactive", "dead")
            await K.set_klippy("disconnected", "Klippy Disconnected")
        else:
            await service_state("klipper", "active", "running")
            asyncio.create_task(K.restart())
    return want


# ------------------------------------------------------------------ machine
SERVICES = {"klipper": ["active", "running"], "moonraker": ["active", "running"],
            "grumpyscreen": ["active", "running"]}


async def service_state(name, active, sub):
    SERVICES[name] = [active, sub]
    await notify("notify_service_state_changed", {name: {"active_state": active, "sub_state": sub}})


def is_printing():
    return S.KLIPPY["state"] == "ready" and STATE.get("print_stats", {}).get("state") == "printing"


def system_info():
    return {"system_info": {
        "python": {"version": [3, 11, 2, "final", 0], "version_string": "3.11.2 (main, Mar 13 2023) [GCC 12.2.0]"},
        "cpu_info": {"cpu_count": 4, "bits": "64bit", "processor": "aarch64", "cpu_desc": "",
                     "serial_number": "5c0ff33d", "hardware_desc": "", "model": "Grumpy Sim Host",
                     "total_memory": 1015632, "memory_units": "kB"},
        "sd_info": {},
        "distribution": {"name": "Debian GNU/Linux 12 (bookworm)", "id": "debian", "version": "12",
                         "version_parts": {"major": "12", "minor": "", "build_number": ""}, "like": "",
                         "codename": "bookworm", "release_info": {}, "kernel_version": "6.1.0-sim"},
        "virtualization": {"virt_type": "none", "virt_identifier": "none"},
        "network": {"wlan0": {"mac_address": "aa:bb:cc:dd:ee:ff",
                              "ip_addresses": [{"family": "ipv4", "address": "192.168.1.148", "is_link_local": False}]}},
        "canbus": {}, "provider": "systemd_dbus",
        "available_services": sorted(SERVICES),
        "service_state": {k: {"active_state": v[0], "sub_state": v[1]} for k, v in SERVICES.items()},
        "instance_ids": {"moonraker": "moonraker", "klipper": "klipper"}}}


PROC = {"samples": [], "rx": 48471767, "tx": 113430843}


def proc_sample():
    t = time.time()
    busy = 25.0 if K.SD.active() else 4.0
    cpu = round(busy + 6 * abs(math.sin(t / 17)), 2)
    PROC["rx"] += random.randint(800, 4000)
    PROC["tx"] += random.randint(500, 2500)
    return {"moonraker_stats": {"time": round(t, 2), "cpu_usage": round(cpu / 3, 2), "memory": 41236, "mem_units": "kB"},
            "cpu_temp": round(46 + cpu / 4 + math.sin(t / 40), 3),
            "network": {"wlan0": {"rx_bytes": PROC["rx"], "tx_bytes": PROC["tx"], "rx_packets": 0, "tx_packets": 0,
                                  "rx_errs": 0, "tx_errs": 0, "rx_drop": 0, "tx_drop": 0,
                                  "bandwidth": round(random.uniform(1500, 6000), 2)}},
            "system_cpu_usage": {"cpu": cpu, "cpu0": round(cpu * 1.2, 2), "cpu1": round(cpu * 0.8, 2),
                                 "cpu2": round(cpu * 0.9, 2), "cpu3": round(cpu * 1.1, 2)},
            "system_memory": {"total": 1015632, "available": 612400, "used": 403232},
            "websocket_connections": len(S.CLIENTS)}


TEMPS = {}


async def stats_loop():
    """notify_proc_stat_update once a second, and the temperature store."""
    while True:
        await asyncio.sleep(1)
        sample = proc_sample()
        PROC["samples"] = (PROC["samples"] + [sample["moonraker_stats"]])[-30:]
        await notify("notify_proc_stat_update", sample)
        if S.KLIPPY["state"] == "ready":
            for name in STATE.get("heaters", {}).get("available_sensors", []):
                obj = STATE.get(name, {})
                rec = TEMPS.setdefault(name, {})
                for field in ("temperature", "target", "power", "speed"):
                    if field in obj:
                        lst = rec.setdefault(field + "s", [])
                        lst.append(round(obj[field] or 0.0, 2))
                        del lst[:-1200]


async def host_down(seconds, reason, klippy_restart=True):
    """Reboot / shutdown / moonraker restart: every client is dropped, the port
    refuses connections for a while, then the host comes back up."""
    await console("// %s" % reason)
    S.HOST["up"] = False
    for ws in list(S.CLIENTS):
        asyncio.create_task(ws.close())
    if klippy_restart:
        S.KLIPPY.update(state="disconnected", message="Klippy Disconnected")
        await HISTORY.finish_current("server_exit")
    await asyncio.sleep(seconds)
    S.HOST["up"] = True
    if klippy_restart:
        asyncio.create_task(K.restart())


# ------------------------------------------------------------------ update manager
UPDATE = {"busy": False, "entries": {}, "mode": 0, "refreshed": False}


def update_mode():
    """--update takes a list; each refresh moves on to the next mode."""
    modes = [m.strip() for m in S.ARGS.update.split(",") if m.strip()] or ["ok"]
    return modes[UPDATE["mode"] % len(modes)]


def git_entry(base, name, owner, version, sha):
    url = "https://github.com/%s/%s.git" % (owner, name)
    return dict(base, name=name, configured_type="git_repo", detected_type="git_repo", channel="dev",
                repo_detected=True, remote_alias="origin", branch="master", owner=owner, repo_name=name,
                version=version, remote_version=version, rollback_version=version, current_hash=sha * 5,
                remote_hash=sha * 5, is_dirty=False, detached=False, commits_behind=[], commits_behind_count=0,
                git_messages=[], full_version_string="%s-g%s" % (version, sha), pristine=True, corrupt=False,
                recovery_url=url, remote_url=url)


def build_updates():
    scen = update_mode()
    base = {"channel": "stable", "debug_enabled": False, "channel_invalid": False, "is_valid": True,
            "info_tags": [], "warnings": [], "anomalies": []}
    UPDATE["entries"] = {
        "system": {"name": "system", "configured_type": "system", "package_count": 0, "package_list": []},
        "moonraker": git_entry(base, "moonraker", "Arksine", "v0.9.3-123", "9e676eba"),
        "klipper": git_entry(base, "klipper", "Klipper3d", "v0.13.0-40", "5ea84ea0"),
    }
    if scen == "missing":
        return
    cosmos = dict(base, name="cosmos", configured_type="web", repo_name="cosmos", owner="pellcorp",
                  version="v1.4.0", remote_version="v1.4.0", rollback_version="v1.3.2", last_error="?",
                  info_tags=["desc=COSMOS firmware"])
    if scen in ("ok", "fail", "refuse"):
        cosmos["remote_version"] = "v1.5.0"
    elif scen == "nightly":
        cosmos.update(configured_type="git_repo", channel="dev", version="v1.4.0-12", remote_version="v1.4.0-15",
                      commits_behind_count=3, branch="main", detected_type="git_repo",
                      commits_behind=[{"sha": "%040x" % i, "author": "pellcorp", "date": str(int(time.time()) - i * 3600),
                                       "subject": s, "message": "", "tag": None}
                                      for i, s in enumerate(["Fix wifi restart", "Bump grumpyscreen", "Tidy installer"])])
    elif scen == "invalid":
        cosmos.update(is_valid=False, warnings=["Repo has been modified locally",
                                                "Remote 'origin' does not match the recovery url"])
    UPDATE["busy"] = scen == "busy"
    UPDATE["entries"]["cosmos"] = cosmos


def update_status():
    return {"github_rate_limit": 60, "github_requests_remaining": 57,
            "github_limit_reset_time": float(int(time.time()) + 3000),
            "version_info": {k: dict(v) for k, v in UPDATE["entries"].items()}, "busy": UPDATE["busy"]}


async def run_update(name):
    if is_printing() or update_mode() == "refuse":
        raise RpcError("Update Refused: Klippy is printing", 503)
    if UPDATE["busy"]:
        raise RpcError("Item %s is currently updating" % name, 503)
    entry = UPDATE["entries"].get(name)
    if entry is None:
        raise RpcError("Updater %s not available" % name, -32601)
    behind = entry.get("commits_behind_count", 0) > 0
    if entry["version"] == entry["remote_version"] and not behind:
        return "ok"
    UPDATE["busy"] = True
    pid = random.getrandbits(40)
    git = entry["configured_type"] == "git_repo"
    prefix = ("Git Repo %s: " if git else "Web Client %s: ") % name

    async def say(msg, complete=False):
        await notify("notify_update_response", {"application": name, "proc_id": pid,
                                                "message": msg, "complete": complete})
        await asyncio.sleep(0.6)

    try:
        await say("Updating Application %s..." % name)
        await say(prefix + ("Updating Repo..." if git else "Downloading Release..."))
        if not git:
            for pct in (21, 48, 77, 100):
                await say("Downloading %s: %d KiB / 1420 KiB [%d%%]" % (name, 1420 * pct // 100, pct))
        else:
            await say("remote: Enumerating objects: 14, done.\nUpdating 3f1c2a9..8d0e4b7\nFast-forward")
        if update_mode() == "fail":
            await say("Error updating %s: Unable to locate update" % name, complete=True)
            raise RpcError("Unable to locate update")
        await say(prefix + "Restarting service klipper...")
        await say(prefix + "Update Finished...", complete=True)
        entry["version"] = entry["remote_version"]
        entry["commits_behind_count"] = 0
        entry["commits_behind"] = []
        asyncio.create_task(K.restart())
        return "ok"
    finally:
        UPDATE["busy"] = False


# ------------------------------------------------------------------ dispatch
def p(params, key):
    if key not in params:
        raise RpcError("No data for argument: %s" % key)
    return params[key]


async def handle(ws, method, params):
    if method.startswith("printer."):
        require_klippy()
    h = METHODS.get(method)
    if h is None:
        raise RpcError("Method not found", -32601)
    result = h(ws, params)
    return await result if asyncio.iscoroutine(result) else result


def later(coro):
    """Start something that outlives the request (restarts, host down)."""
    asyncio.create_task(coro)
    return "ok"


async def locked(script):
    """pause_resume's webhooks run their gcode on klipper's mutex."""
    async with GCODE_LOCK:
        await K.run_line(script)
    return "ok"


def identify(ws, params):
    c = S.CLIENTS[ws]
    if c.get("info"):
        raise RpcError("Connection already identified: %r" % c["info"])
    t = str(p(params, "type")).lower()
    if t not in ("web", "mobile", "desktop", "display", "bot", "agent", "other"):
        raise RpcError("Invalid Client Type: %s" % t)
    c["info"] = {"name": p(params, "client_name"), "version": p(params, "version"), "type": t,
                 "url": p(params, "url")}
    return {"connection_id": c["id"]}


def server_info(ws, params):
    comps = ["database", "file_manager", "klippy_apis", "machine", "data_store", "proc_stats", "job_state",
             "job_queue", "history", "update_manager", "announcements", "webcam"]
    comps += ["power"] if POWER else []
    comps += ["spoolman"] if S.ARGS.spoolman != "off" else []
    return {"klippy_connected": S.KLIPPY["state"] != "disconnected", "klippy_state": S.KLIPPY["state"],
            "components": comps, "failed_components": [], "registered_directories": ["config", "gcodes", "logs"],
            "warnings": [], "websocket_count": len(S.CLIENTS), "moonraker_version": "v0.9.3-123-g9e676eba",
            "missing_klippy_requirements": [], "api_version": [1, 5, 0], "api_version_string": "1.5.0"}


def printer_info(ws, params):
    return {"state": S.KLIPPY["state"], "state_message": S.KLIPPY["message"], "hostname": "grumpy-sim",
            "klipper_path": "/home/pi/klipper", "config_file": "/home/pi/printer_data/config/printer.cfg",
            "software_version": "v0.13.0-40-g5ea84ea0", "cpu_info": "4 core aarch64"}


def gcode_store(ws, params):
    count = int(params.get("count", 100))
    items = list(S.GCODE_STORE)
    return {"gcode_store": items[-count:] if count else items}


def objects_param(params):
    return {k: (v or None) for k, v in params.get("objects", {}).items()}


def subscribe(ws, params):
    S.CLIENTS[ws]["subs"] = subs = objects_param(params)
    return {"eventtime": S.eventtime(), "status": S.snapshot(subs)}


def metascan(ws, params):
    name = p(params, "filename")
    if not os.path.isfile(os.path.join(files.root(), name)):
        raise RpcError("File '%s' does not exist" % name, -32601)
    files.scan(name)
    return files.metadata(name)


def thumbnails(ws, params):
    name = p(params, "filename")
    d = os.path.dirname(name)
    return [{"width": t["width"], "height": t["height"], "size": t["size"],
             "thumbnail_path": os.path.join(d, t["relative_path"])}
            for t in files.METADATA.get(name, {}).get("thumbnails", [])]


def history_list(ws, params):
    order = str(params.get("order", "desc")).upper()
    if order not in ("ASC", "DESC"):
        raise RpcError("Invalid `order` value: %s" % order)
    since, before = float(params.get("since", -1)), float(params.get("before", -1))
    jobs = [j for j in HISTORY.jobs if (since < 0 or j["start_time"] > since)
            and (before < 0 or (j["end_time"] or 1e18) < before)]
    jobs.sort(key=lambda j: j["job_id"], reverse=order == "DESC")
    start, limit = int(params.get("start", 0)), int(params.get("limit", 50))
    jobs = jobs[start:start + limit] if limit > 0 else jobs[start:]
    return {"count": len(HISTORY.jobs), "jobs": [HISTORY.public(j) for j in jobs]}


def job_by_uid(uid):
    try:
        n = int(uid, 16)
    except ValueError as e:
        raise RpcError(str(e), 500)
    job = next((j for j in HISTORY.jobs if int(j["job_id"], 16) == n), None)
    if job is None:
        raise RpcError("Invalid job uid: %s" % uid, -32601)
    return job


def history_delete(ws, params):
    jobs = list(HISTORY.jobs) if params.get("all") else [job_by_uid(p(params, "uid"))]
    for job in jobs:
        HISTORY.jobs.remove(job)
    return {"deleted_jobs": [j["job_id"] for j in jobs]}


def reset_totals(ws, params):
    if HISTORY.current:
        raise RpcError("Job in progress, cannot reset totals")
    last = dict(HISTORY.totals)
    HISTORY.totals = {k: 0 if k == "total_jobs" else 0.0 for k in last}
    return {"last_totals": last, "last_auxiliary_totals": []}


def queue_delete(ws, params):
    ids = set(params.get("job_ids", []))
    QUEUE.jobs = [] if params.get("all") else [j for j in QUEUE.jobs if j["job_id"] not in ids]
    asyncio.create_task(QUEUE.changed("jobs_removed"))
    return QUEUE.result()


def queue_pause(ws, params):
    QUEUE.state = "paused"
    asyncio.create_task(QUEUE.changed("state_changed"))
    return QUEUE.result()


def queue_jump(ws, params):
    jid = p(params, "job_id")
    job = next((j for j in QUEUE.jobs if j["job_id"] == jid), None)
    if job is None:
        raise RpcError("Invalid job id: %s" % jid)
    QUEUE.jobs.remove(job)
    QUEUE.jobs.insert(0, job)
    return QUEUE.result()


def proc_stats(ws, params):
    s = proc_sample()
    s.update(moonraker_stats=PROC["samples"] or [s["moonraker_stats"]], throttled_state=None,
             system_uptime=round(time.time() - S.START + 86400.0, 2))
    return s


async def service(params, action):
    name = p(params, "service")
    if name == "moonraker":
        if action != "restart":
            raise RpcError("Service action '%s' not available for moonraker" % action)
        return later(host_down(2, "Moonraker restarting", klippy_restart=False))
    if name not in SERVICES:
        raise RpcError("Service '%s' not allowed" % name)
    if name == "klipper":
        if action == "stop":
            await service_state("klipper", "inactive", "dead")
            await K.set_klippy("disconnected", "Klippy Disconnected")
        else:
            await service_state("klipper", "active", "running")
            asyncio.create_task(K.restart())
    return "ok"


def get_device(ws, params):
    name = p(params, "device")
    if name not in POWER:
        raise RpcError("No valid device named %s" % name)
    return {name: POWER[name]["status"]}


async def post_device(ws, params):
    name = p(params, "device")
    return {name: await set_power(name, p(params, "action"))}


async def power_many(params, action):
    if not params:
        raise RpcError("No arguments provided")
    out = {}
    for name in params:
        if name not in POWER:
            out[name] = "device_not_found"
        else:
            out[name] = POWER[name]["status"] if action is None else await set_power(name, action)
    return out


def update_refresh(ws, params):
    if UPDATE["refreshed"]:
        UPDATE["mode"] += 1
        build_updates()
    UPDATE["refreshed"] = True
    if UPDATE["busy"] or is_printing() or update_mode() == "refuse":
        raise RpcError("Server is busy, cannot perform refresh", 503)
    name = params.get("name")
    if name and name not in UPDATE["entries"]:
        raise RpcError("No updater registered for '%s'" % name)
    st = update_status()

    async def refreshed():
        await asyncio.sleep(0.2)
        await notify("notify_update_refreshed", st)
    asyncio.create_task(refreshed())
    return st


async def full_update():
    for name, e in UPDATE["entries"].items():
        if e.get("version") != e.get("remote_version") or e.get("commits_behind_count"):
            await run_update(name)
    return "ok"


def update_app(name):
    """machine.update.<app>; client/upgrade take the app as a parameter."""
    def h(ws, params):
        app = params.get("name") if name in ("client", "upgrade") else name
        return run_update(app) if app else full_update()
    return h


def sim_reset(ws, params):
    """Testbench only (tools/sim_hook.py factory-reset): the session back to its start."""
    files.setup()
    HISTORY.__init__()
    HISTORY.seed(S.ARGS.history)
    QUEUE.__init__()
    mmu.build(S.ARGS)
    build_updates()
    build_power()
    return later(K.restart())


METHODS = {
    "server.connection.identify": identify,
    "server.websocket.id": lambda ws, params: {"websocket_id": S.CLIENTS[ws]["id"]},
    "server.info": server_info,
    "server.restart": lambda ws, params: later(host_down(2, "Moonraker restarting", klippy_restart=False)),
    "server.gcode_store": gcode_store,
    "server.temperature_store": lambda ws, params: {k: dict(v) for k, v in TEMPS.items()},
    "server.webcams.list": lambda ws, params: {"webcams": []},
    "server.announcements.list": lambda ws, params: {"entries": [], "feeds": ["moonraker", "klipper"]},
    "server.files.roots": lambda ws, params: files.roots(),
    "server.files.list": lambda ws, params: files.file_list(params.get("root", "gcodes")),
    "server.files.get_directory": lambda ws, params: files.get_directory(params.get("path", "gcodes"),
                                                                         bool(params.get("extended"))),
    "server.files.metadata": lambda ws, params: files.metadata(p(params, "filename")),
    "server.files.metascan": metascan,
    "server.files.thumbnails": thumbnails,
    "server.files.delete_file": lambda ws, params: files.delete_file(p(params, "path")),
    "server.files.post_directory": lambda ws, params: files.post_directory(p(params, "path")),
    "server.files.delete_directory": lambda ws, params: files.delete_directory(p(params, "path"),
                                                                               bool(params.get("force"))),
    "server.files.move": lambda ws, params: files.move_copy(p(params, "source"), p(params, "dest"), copy=False),
    "server.files.copy": lambda ws, params: files.move_copy(p(params, "source"), p(params, "dest"), copy=True),
    "server.spoolman.get_spool_id": lambda ws, params: {"spool_id": spoolman.ACTIVE["id"]},
    "server.spoolman.post_spool_id": lambda ws, params: spoolman.set_active(params.get("spool_id")),
    "server.spoolman.proxy": lambda ws, params: spoolman.proxy(params),
    "server.spoolman.status": lambda ws, params: {"spoolman_connected": True, "pending_reports": [],
                                                  "spool_id": spoolman.ACTIVE["id"]},
    "server.history.list": history_list,
    "server.history.get_job": lambda ws, params: {"job": HISTORY.public(job_by_uid(p(params, "uid")))},
    "server.history.delete_job": history_delete,
    "server.history.totals": lambda ws, params: {"job_totals": dict(HISTORY.totals), "auxiliary_totals": []},
    "server.history.reset_totals": reset_totals,
    "server.job_queue.status": lambda ws, params: QUEUE.result(),
    "server.job_queue.post_job": lambda ws, params: QUEUE.post(p(params, "filenames"), bool(params.get("reset"))),
    "server.job_queue.delete_job": queue_delete,
    "server.job_queue.pause": queue_pause,
    "server.job_queue.start": lambda ws, params: QUEUE.start(),
    "server.job_queue.jump": queue_jump,
    "machine.system_info": lambda ws, params: system_info(),
    "machine.proc_stats": proc_stats,
    "machine.reboot": lambda ws, params: later(host_down(S.ARGS.reboot_time, "Rebooting the host")),
    "machine.shutdown": lambda ws, params: later(host_down(
        S.ARGS.shutdown_time, "Host is shutting down (sim: back in %ds)" % S.ARGS.shutdown_time)),
    "machine.services.restart": lambda ws, params: service(params, "restart"),
    "machine.services.stop": lambda ws, params: service(params, "stop"),
    "machine.services.start": lambda ws, params: service(params, "start"),
    "machine.device_power.devices": lambda ws, params: {"devices": [dict(d) for d in POWER.values()]},
    "machine.device_power.get_device": get_device,
    "machine.device_power.post_device": post_device,
    "machine.device_power.status": lambda ws, params: power_many(params, None),
    "machine.device_power.on": lambda ws, params: power_many(params, "on"),
    "machine.device_power.off": lambda ws, params: power_many(params, "off"),
    "machine.update.status": lambda ws, params: update_status(),
    "machine.update.refresh": update_refresh,
    "machine.update.full": lambda ws, params: full_update(),
    **{"machine.update." + n: update_app(n) for n in ("client", "moonraker", "klipper", "system", "upgrade")},
    "printer.info": printer_info,
    "printer.objects.list": lambda ws, params: {"objects": list(STATE)},
    "printer.objects.query": lambda ws, params: {"eventtime": S.eventtime(),
                                                 "status": S.snapshot(objects_param(params))},
    "printer.objects.subscribe": subscribe,
    "printer.query_endstops.status": lambda ws, params: {"x": "open", "y": "open", "z": "open"},
    "printer.gcode.script": lambda ws, params: run_gcode(p(params, "script")),
    "printer.gcode.help": lambda ws, params: K.help_table(),
    "printer.print.start": lambda ws, params: run_gcode('SDCARD_PRINT_FILE FILENAME="%s"' % (
        p(params, "filename").lstrip("/").replace('"', '\\"'))),
    "printer.print.pause": lambda ws, params: locked("PAUSE"),
    "printer.print.resume": lambda ws, params: locked("RESUME"),
    "printer.print.cancel": lambda ws, params: locked("CANCEL_PRINT"),
    "printer.emergency_stop": lambda ws, params: later(K.shutdown("Shutdown due to webhooks request")),
    "printer.restart": lambda ws, params: later(K.restart()),
    "printer.firmware_restart": lambda ws, params: later(K.restart()),
    "sim.reset": sim_reset,
}

K.HOOKS.update(print_event=print_event, klippy_state=klippy_state, sd_line=sd_line)
