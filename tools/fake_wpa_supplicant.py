#!/usr/bin/env python3
"""Enough of a wpa_supplicant control socket to drive the wifi panel.

The panel is the one part of the UI that cannot be exercised against
fake_moonraker, because it does not go through Moonraker at all: it opens
wpa_supplicant's UNIX control socket directly and speaks its text protocol.
Which means the wifi code was, until this existed, only ever tested by running
it on a printer, and getting it wrong locks someone out of their own network.

Usage:

    python3 tools/fake_wpa_supplicant.py --dir /tmp/fake-wpa

Then point the simulator at it by setting WPA_SUPPLICANT_SOCKET environment to that directory, like:

WPA_SUPPLICANT_SOCKET=/tmp/fake-wpa build/sdl/bin/grumpyscreen

    --fail wrong-key      the next connect attempt fails, the common case
    --fail assoc-reject   the access point refuses the association
    --fail not-found      the SSID is not there
    --known "SSID:psk"    seed a saved network, repeatable

The protocol is datagrams both ways. A client binds its own socket, sends a
command, and reads the reply from the same socket. A client that has sent
ATTACH also receives unsolicited event lines, which is how a scan completing or
an authentication failing reaches the panel.
"""

import argparse, os, socket, sys, threading, time

# What a scan finds. bssid, frequency, signal level, flags, ssid, tab
# separated, with a header line the panel skips by its "bss" prefix.
NETWORKS = [
    ("02:00:00:00:01:00", 2437, -42, "[WPA2-PSK-CCMP][ESS]", "Ossining Guest"),
    ("02:00:00:00:02:00", 5180, -55, "[WPA2-PSK-CCMP][ESS]", "printer-net"),
    ("02:00:00:00:03:00", 2412, -71, "[WPA2-PSK-CCMP][ESS]", "Cafe Wifi"),
    ("02:00:00:00:04:00", 2462, -88, "[ESS]", "open-hotspot"),
]

FAIL_EVENTS = {
    "wrong-key": [
        "CTRL-EVENT-SSID-TEMP-DISABLED id=%(id)s ssid=\"%(ssid)s\" "
        "auth_failures=1 duration=10 reason=WRONG_KEY",
    ],
    "assoc-reject": [
        "CTRL-EVENT-ASSOC-REJECT bssid=02:00:00:00:02:00 status_code=17",
    ],
    "not-found": [
        "CTRL-EVENT-NETWORK-NOT-FOUND",
    ],
}


class FakeSupplicant:
    def __init__(self, path, fail, known):
        self.path = path
        self.fail = fail
        self.monitors = set()
        self.lock = threading.Lock()
        # id -> {ssid, psk, enabled, current}
        self.networks = {}
        self.next_id = 0
        for entry in known:
            ssid, _, psk = entry.partition(":")
            nid = self._add()
            self.networks[nid].update(ssid=ssid, psk=psk or "seeded")

        if os.path.exists(path):
            os.unlink(path)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.sock.bind(path)

    def _add(self):
        nid = str(self.next_id)
        self.next_id += 1
        self.networks[nid] = {"ssid": "", "psk": "", "enabled": False,
                              "current": False}
        return nid

    def event(self, text):
        """Push an unsolicited line to every attached client.

        The <3> is the priority prefix wpa_supplicant puts on these, and the
        panel matches on it, so it has to be here rather than implied.
        """
        line = ("<3>" + text).encode()
        with self.lock:
            for peer in list(self.monitors):
                try:
                    self.sock.sendto(line, peer)
                except OSError:
                    self.monitors.discard(peer)
        print("  event: " + text, flush=True)

    def later(self, delay, fn):
        threading.Timer(delay, fn).start()

    def handle(self, cmd, peer):
        head, _, rest = cmd.partition(" ")

        if head == "ATTACH":
            with self.lock:
                self.monitors.add(peer)
            return "OK\n"
        if head == "DETACH":
            with self.lock:
                self.monitors.discard(peer)
            return "OK\n"

        if head == "SCAN":
            self.later(0.6, lambda: self.event("CTRL-EVENT-SCAN-RESULTS "))
            return "OK\n"

        if head == "SCAN_RESULTS":
            out = ["bssid / frequency / signal level / flags / ssid"]
            for bssid, freq, level, flags, ssid in NETWORKS:
                out.append(f"{bssid}\t{freq}\t{level}\t{flags}\t{ssid}")
            return "\n".join(out) + "\n"

        if head == "LIST_NETWORKS":
            out = ["network id / ssid / bssid / flags"]
            for nid, n in sorted(self.networks.items(), key=lambda kv: int(kv[0])):
                flags = "[CURRENT]" if n["current"] else ("" if n["enabled"] else "[DISABLED]")
                out.append(f"{nid}\t{n['ssid']}\tany\t{flags}")
            return "\n".join(out) + "\n"

        if head == "ADD_NETWORK":
            # Trailing newline on purpose: this is what wpa_supplicant returns
            # and the panel used to interpolate it straight into the next
            # command without stripping it.
            return self._add() + "\n"

        if head == "SET_NETWORK":
            nid, _, kv = rest.partition(" ")
            key, _, value = kv.partition(" ")
            if nid not in self.networks:
                return "FAIL\n"
            self.networks[nid][key] = value.strip('"')
            return "OK\n"

        if head == "ENABLE_NETWORK":
            if rest not in self.networks:
                return "FAIL\n"
            self.networks[rest]["enabled"] = True
            return "OK\n"

        if head == "REMOVE_NETWORK":
            if self.networks.pop(rest, None) is None:
                return "FAIL\n"
            print(f"  removed network {rest}", flush=True)
            return "OK\n"

        if head == "SELECT_NETWORK":
            nid = rest
            if nid not in self.networks:
                return "FAIL\n"
            for n in self.networks.values():
                n["current"] = False
            ssid = self.networks[nid]["ssid"]

            if self.fail:
                for text in FAIL_EVENTS[self.fail]:
                    self.later(1.2, lambda t=text: self.event(
                        t % {"id": nid, "ssid": ssid}))
            else:
                self.networks[nid]["current"] = True
                self.later(1.2, lambda: self.event(
                    "CTRL-EVENT-CONNECTED - Connection to 02:00:00:00:02:00 "
                    "completed [id=%s id_str=]" % nid))
            return "OK\n"

        if head == "SAVE_CONFIG":
            return "OK\n"

        return "UNKNOWN COMMAND\n"

    def serve(self):
        print(f"fake wpa_supplicant on {self.path}"
              + (f", next connect fails: {self.fail}" if self.fail else ""),
              flush=True)
        while True:
            data, peer = self.sock.recvfrom(4096)
            cmd = data.decode(errors="replace").strip()
            print(f"cmd: {cmd}", flush=True)
            try:
                reply = self.handle(cmd, peer)
            except Exception as e:              # a fake should never take the UI down
                print(f"  error: {e}", flush=True)
                reply = "FAIL\n"
            if peer:
                self.sock.sendto(reply.encode(), peer)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dir", default="/tmp/fake-wpa",
                    help="directory to put the control socket in")
    ap.add_argument("--iface", default="wlan0",
                    help="socket name, which is the interface name")
    ap.add_argument("--fail", choices=sorted(FAIL_EVENTS),
                    help="make connect attempts fail this way")
    ap.add_argument("--known", action="append", default=[],
                    help='seed a saved network, "SSID" or "SSID:psk"')
    args = ap.parse_args()

    os.makedirs(args.dir, exist_ok=True)
    path = os.path.join(args.dir, args.iface)
    fake = FakeSupplicant(path, args.fail, args.known)
    try:
        fake.serve()
    except KeyboardInterrupt:
        pass
    finally:
        os.unlink(path)


if __name__ == "__main__":
    main()
