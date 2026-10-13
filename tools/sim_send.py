#!/usr/bin/env python3
"""Poke a running fake moonraker: send gcode or a raw rpc, print the reply.

  python3 tools/sim_send.py "G28 X"                 # a gcode command
  python3 tools/sim_send.py SIM_PROMPT              # a testbench hook
  python3 tools/sim_send.py --rpc printer.info
  python3 tools/sim_send.py --rpc server.spoolman.proxy '{"path":"/v1/spool"}'
  python3 tools/sim_send.py --listen 10             # watch the notifications

Useful testbench gcode: SIM_PRINT, SIM_SHUTDOWN, SIM_READY, SIM_PROMPT,
SIM_UPLOAD, SIM_RUNOUT LANE=lane1, SIM_ERROR (hh only).
"""
import argparse
import asyncio
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from moonsim import ws  # noqa: E402


async def run(args):
    conn = await ws.connect(args.port)
    try:
        if args.command or args.rpc:
            method = args.rpc or "printer.gcode.script"
            params = (json.loads(args.command) if args.rpc and args.command.startswith("{")
                      else {"script": args.command} if not args.rpc else {})
            await conn.send(json.dumps({"jsonrpc": "2.0", "id": 1,
                                      "method": method, "params": params}))
        deadline = asyncio.get_event_loop().time() + (args.listen or 30)
        while True:
            timeout = max(0.0, deadline - asyncio.get_event_loop().time())
            try:
                raw = await asyncio.wait_for(conn.recv(), timeout or 0.01)
            except (asyncio.TimeoutError, ws.Closed):
                return
            msg = json.loads(raw)
            if msg.get("id") == 1:
                print(json.dumps(msg.get("result", msg.get("error")), indent=2)[:4000])
                if not args.listen:
                    return
            elif args.listen:
                print(msg["method"], json.dumps(msg.get("params"))[:300])
    finally:
        await conn.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("command", nargs="?", default="",
                    help="gcode script, or json params when --rpc is given")
    ap.add_argument("--port", type=int, default=7125)
    ap.add_argument("--rpc", default="", help="send this method instead of gcode")
    ap.add_argument("--listen", type=float, default=0,
                    help="keep printing notifications for N seconds")
    asyncio.run(run(ap.parse_args()))
