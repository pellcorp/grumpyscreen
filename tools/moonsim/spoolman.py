"""Spoolman behind moonraker's proxy: a small writable spool database.

Moonraker forwards server.spoolman.proxy requests to spoolman's REST API, and
reports filament use against the active spool, so the simulator keeps both.
"""
from .state import RpcError, notify

CATALOGUE = [  # name, material, colour, vendor; Fiberon starts archived
    ("PolyLite", "PLA", "E53935", "Polymaker"),
    ("PolyTerra", "PLA", "1E88E5", "Polymaker"),
    ("PolyMax", "PETG", "43A047", "Polymaker"),
    ("Fiberon", "PA6-CF", "212121", "Fiberology"),
    ("Galaxy", "PLA", "7E57C2", "Prusament"),
    ("Bricks", "ABS", "FB8C00", "Prusament"),
]
GRAMS_PER_MM = 0.00298      # 1.75 mm filament at 1.24 g/cm3

SPOOLS = []
ACTIVE = {"id": None}


def build(rows):
    """rows: [(id, name, material, colour hex, vendor)]; empty -> the catalogue."""
    SPOOLS.clear()
    rows = rows or [(i, n, m, c, v) for i, (n, m, c, v) in enumerate(CATALOGUE, 1)]
    for i, (sid, name, mat, col, vendor) in enumerate(rows):
        SPOOLS.append({"id": sid, "name": name, "material": mat, "color": col, "vendor": vendor,
                       "initial": 1000.0, "remaining": max(60.0, 863.0 - (i % 7) * 137.0),
                       "archived": name == "Fiberon"})
    ACTIVE["id"] = 2 if find(2) else None


def find(sid):
    return next((s for s in SPOOLS if s["id"] == sid), None)


def spool_json(s):
    return {
        "id": s["id"], "archived": s["archived"],
        "registered": "2026-01-04T09:15:00Z",
        "first_used": "2026-01-12T08:31:00Z", "last_used": "2026-09-01T17:02:00Z",
        "initial_weight": s["initial"],
        "remaining_weight": round(s["remaining"], 1),
        "used_weight": round(s["initial"] - s["remaining"], 1),
        "remaining_length": round(s["remaining"] / GRAMS_PER_MM, 1),
        "filament": {"id": s["id"], "name": s["name"], "material": s["material"],
                     "color_hex": s["color"], "density": 1.24, "diameter": 1.75,
                     "spool_weight": 210.0, "vendor": {"id": 1, "name": s["vendor"]}},
    }


def use(spool_id, mm):
    s = find(spool_id) if spool_id else None
    if s:
        s["remaining"] = max(0.0, s["remaining"] - mm * GRAMS_PER_MM)


async def proxy(params):
    verb = params.get("request_method", "GET").upper()
    path = params.get("path", "/v1/spool")
    body = params.get("body") or {}
    head, _, query = path.partition("?")
    if head == "/v1/spool" and verb == "GET":
        return [spool_json(s) for s in SPOOLS if "allow_archived=true" in query or not s["archived"]]
    if head.startswith("/v1/spool/") and head[10:].isdigit():
        spool = find(int(head[10:]))
        if spool is None:
            raise RpcError("Spoolman returned 404 for %s" % path, 404)
        if verb in ("PATCH", "PUT"):
            if "archived" in body:
                spool["archived"] = bool(body["archived"])
            if "remaining_weight" in body:
                spool["remaining"] = float(body["remaining_weight"])
        elif verb == "DELETE":
            SPOOLS.remove(spool)
        return spool_json(spool)
    if head == "/v1/info" and verb == "GET":
        return {"version": "0.22.0-mock", "debug_mode": False}
    if head == "/v1/health":
        return {"status": "healthy"}
    raise RpcError("Spoolman returned 404 for %s %s" % (verb, path), 404)


async def set_active(spool_id):
    if spool_id is not None and find(spool_id) is None:
        raise RpcError("Spool id %s does not exist" % spool_id, 404)
    ACTIVE["id"] = spool_id
    await notify("notify_active_spool_set", {"spool_id": spool_id})
    return {"spool_id": spool_id}
