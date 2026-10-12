#!/usr/bin/env python3
"""A wpa_supplicant control socket for driving the wifi panel without a printer.

The panel talks to wpa_supplicant directly over its UNIX control socket, not
through Moonraker, so this is the other half of the simulator. Datagrams both
ways; clients that send ATTACH also get event lines. Nothing is written to
disk: saved networks last until it exits.

    python3 tools/fake_wpa_supplicant.py --dir /tmp/fake-wpa
    WPA_SUPPLICANT_SOCKET=/tmp/fake-wpa grumpyscreen

It starts joined to "HomeNet". Every secured network's password is
"printer123" (--password); a wrong one fails with WRONG_KEY. As in
wpa_supplicant, a passphrase outside 8-63 characters is refused at
SET_NETWORK, and the open "Cafe Free WiFi" only matches a network configured
with key_mgmt NONE. "Neighbour" connects, then drops after a few seconds.

    --fail wrong-key|assoc-reject|not-found   every connect attempt fails that way
    --known "SSID[:psk]"   seed a saved network, repeatable
    --connected SSID       the network it starts joined to; none for nothing

tools/sim_hook.py sends SIM_RESTART (drop and rejoin, as restart_wifi does)
and SIM_RESET (back to the starting networks).
"""
import argparse
import os
import random
import socket
import threading

PASSWORD = "printer123"
FAIL = {"mode": None}
FAIL_EVENTS = {
    "wrong-key": ['CTRL-EVENT-SSID-TEMP-DISABLED id=%(id)d ssid="%(ssid)s" auth_failures=1 '
                  "duration=10 reason=WRONG_KEY"],
    "assoc-reject": ["CTRL-EVENT-ASSOC-REJECT bssid=%(bssid)s status_code=17"],
    "not-found": ["CTRL-EVENT-NETWORK-NOT-FOUND"],
}
OPEN_SSID = "Cafe Free WiFi"
FLAKY_SSID = "Neighbour"

# ssid, signal (dBm); the first one is the network we start connected to
ACCESS_POINTS = [("HomeNet", -42), ("Workshop_5G", -55), ("PrinterFarm", -63),
                 ("Neighbour", -71), ("Cafe Free WiFi", -80), ("Garage_IoT", -58),
                 ("Voron-2.4", -66), ("Guest", -74), ("Upstairs_Office", -77),
                 ("BT-Hub-9F2A", -83)]

networks = {}
START = {"ssid": "HomeNet", "known": []}


def reset():
    networks.clear()
    if START["ssid"]:
        networks[0] = {"ssid": START["ssid"], "current": True, "psk": PASSWORD, "key_mgmt": "WPA-PSK"}
    for entry in START["known"]:
        ssid, _, psk = entry.partition(":")
        networks[len(networks)] = {"ssid": ssid, "current": False, "psk": psk or PASSWORD,
                                   "key_mgmt": "NONE" if ssid == OPEN_SSID else "WPA-PSK"}


attached = set()
sock = None


def send_event(msg):
    for addr in list(attached):
        try:
            sock.sendto(msg.encode(), addr)
        except OSError:
            attached.discard(addr)


def later(delay, msg):
    threading.Timer(delay, send_event, args=(msg,)).start()


def bssid(ssid):
    return "00:11:22:33:44:%02x" % next(
        (i for i, (name, _s) in enumerate(ACCESS_POINTS) if name == ssid), 0)


def disconnect(ssid, reason):
    for n in networks.values():
        if n["ssid"] == ssid:
            n["current"] = False
    send_event("<3>CTRL-EVENT-DISCONNECTED bssid=%s reason=%d" % (bssid(ssid), reason))


def connect(nid):
    """The join attempt, a second and a half later, succeeding or not."""
    net = networks.get(nid)
    if net is None:
        return
    ssid, psk = net["ssid"], net.get("psk")
    if FAIL["mode"]:
        net["current"] = False
        for text in FAIL_EVENTS[FAIL["mode"]]:
            send_event("<3>" + text % {"id": nid, "ssid": ssid, "bssid": bssid(ssid)})
        return
    is_open = ssid == OPEN_SSID
    if is_open != (net.get("key_mgmt") == "NONE") or ssid not in [a for a, _s in ACCESS_POINTS]:
        # no scanned BSS matches this network's ssid + key management
        net["current"] = False
        send_event("<3>CTRL-EVENT-NETWORK-NOT-FOUND")
        return
    if not is_open and psk != PASSWORD:
        net["current"] = False
        send_event('<3>CTRL-EVENT-SSID-TEMP-DISABLED id=%d ssid="%s" auth_failures=1 '
                   "duration=10 reason=WRONG_KEY" % (nid, ssid))
        send_event("<3>CTRL-EVENT-DISCONNECTED bssid=%s reason=15" % bssid(ssid))
        return
    for other in networks.values():
        other["current"] = False
    net["current"] = True
    send_event("<3>CTRL-EVENT-CONNECTED - Connection to %s completed [id=%d id_str=]"
               % (bssid(ssid), nid))
    if ssid == FLAKY_SSID:
        threading.Timer(6.0, disconnect, args=(ssid, 4)).start()


def scan_results():
    rows = ["bssid / frequency / signal level / flags / ssid"]
    for ssid, signal in ACCESS_POINTS:
        drift = random.randint(-4, 4)  # the list reorders between scans
        flags = "[ESS]" if ssid == OPEN_SSID else "[WPA2-PSK-CCMP][ESS]"
        freq = 5180 if "5G" in ssid else 2412
        rows.append("%s\t%d\t%d\t%s\t%s" % (bssid(ssid), freq, signal + drift, flags, ssid))
    return "\n".join(rows) + "\n"


def handle(cmd, addr):
    words = cmd.split()
    if cmd == "ATTACH":
        attached.add(addr)
        return "OK"
    if cmd == "DETACH":
        attached.discard(addr)
        return "OK"
    if cmd == "PING":
        return "PONG"
    if cmd == "SCAN":
        later(1.2, "<3>CTRL-EVENT-SCAN-STARTED ")
        later(2.0, "<3>CTRL-EVENT-SCAN-RESULTS ")
        return "OK"
    if cmd == "SCAN_RESULTS":
        return scan_results()
    if cmd == "LIST_NETWORKS":
        rows = ["network id / ssid / bssid / flags"]
        for nid, n in networks.items():
            rows.append("%d\t%s\tany\t%s" % (nid, n["ssid"], "[CURRENT]" if n["current"] else ""))
        return "\n".join(rows) + "\n"
    if cmd == "ADD_NETWORK":
        nid = max(networks, default=-1) + 1
        networks[nid] = {"ssid": "", "current": False, "psk": None, "key_mgmt": "WPA-PSK"}
        return str(nid)
    if words[0] == "SET_NETWORK" and len(words) >= 4:
        nid, field = int(words[1]), words[2]
        raw = cmd.split(" ", 3)[3].strip()
        if nid not in networks:
            return "FAIL"
        if field == "psk":
            if raw.startswith('"'):
                phrase = raw[1:-1] if raw.endswith('"') and len(raw) > 1 else None
                if phrase is None or not 8 <= len(phrase) <= 63:
                    return "FAIL"            # "Invalid passphrase length"
                networks[nid]["psk"] = phrase
            elif len(raw) == 64 and all(c in "0123456789abcdefABCDEF" for c in raw):
                networks[nid]["psk"] = raw
            else:
                return "FAIL"
        elif field == "ssid":
            networks[nid]["ssid"] = raw.strip('"')
        elif field == "key_mgmt":
            networks[nid]["key_mgmt"] = raw
        return "OK"
    if words[0] in ("SELECT_NETWORK", "ENABLE_NETWORK", "DISABLE_NETWORK", "REMOVE_NETWORK") and len(words) == 2:
        nid = int(words[1]) if words[1].isdigit() else -1
        if nid not in networks and not (words[1] == "all" and words[0] != "SELECT_NETWORK"):
            return "FAIL"
        if words[0] == "SELECT_NETWORK":
            threading.Timer(1.5, connect, args=(nid,)).start()
        elif words[0] == "REMOVE_NETWORK":
            for k in (list(networks) if words[1] == "all" else [nid]):
                n = networks.pop(k)
                if n["current"]:
                    later(0.5, "<3>CTRL-EVENT-DISCONNECTED bssid=%s reason=3" % bssid(n["ssid"]))
        return "OK"
    if cmd == "SIM_RESET":
        reset()
        return "OK"
    if cmd == "SIM_RESTART":
        cur = next((k for k, n in networks.items() if n["current"]), None)
        if cur is not None:
            disconnect(networks[cur]["ssid"], 3)
            threading.Timer(3.0, connect, args=(cur,)).start()
        return "OK"
    if words[0] == "DISCONNECT":
        cur = next((n for n in networks.values() if n["current"]), None)
        if cur:
            disconnect(cur["ssid"], 3)
        return "OK"
    if words[0] in ("SAVE_CONFIG", "RECONNECT", "REASSOCIATE", "RECONFIGURE"):
        return "OK"
    if cmd == "STATUS":
        cur = next((n for n in networks.values() if n["current"]), None)
        if not cur:
            return "wpa_state=DISCONNECTED\n"
        return ("bssid=%s\nfreq=2412\nssid=%s\nid=0\nmode=station\n"
                "pairwise_cipher=CCMP\ngroup_cipher=CCMP\nkey_mgmt=WPA2-PSK\n"
                "wpa_state=COMPLETED\nip_address=192.168.1.148\n"
                "address=aa:bb:cc:dd:ee:ff\n" % (bssid(cur["ssid"]), cur["ssid"]))
    return "UNKNOWN COMMAND"


def main():
    global sock, PASSWORD
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="/tmp/fake-wpa", help="directory to put the control socket in")
    ap.add_argument("--iface", default="wlan0",
                    help="socket name; the panel reads its IP off the interface of that name")
    ap.add_argument("--password", default=PASSWORD, help="password of every secured network")
    ap.add_argument("--connected", default="HomeNet",
                    help="saved network the sim starts joined to; 'none' starts with nothing saved")
    ap.add_argument("--fail", choices=sorted(FAIL_EVENTS), help="make connect attempts fail this way")
    ap.add_argument("--known", action="append", default=[], help='seed a saved network, "SSID" or "SSID:psk"')
    args = ap.parse_args()
    FAIL["mode"] = args.fail
    START["known"] = args.known
    PASSWORD = args.password
    START["ssid"] = "" if args.connected == "none" else args.connected
    reset()
    os.makedirs(args.dir, exist_ok=True)
    path = os.path.join(args.dir, args.iface)
    if os.path.exists(path):
        os.unlink(path)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(path)
    print("fake wpa_supplicant at %s; run the UI with WPA_SUPPLICANT_SOCKET=%s"
          % (path, args.dir))
    print("password for every secured network is %r" % PASSWORD)
    while True:
        data, addr = sock.recvfrom(4096)
        cmd = data.decode(errors="replace").strip()
        print("  %s" % cmd.splitlines()[0][:60])
        reply = handle(cmd, addr)
        # every ctrl_iface reply ends in a newline; wpa_ctrl_attach accepts only "OK\n"
        sock.sendto((reply if reply.endswith("\n") else reply + "\n").encode(), addr)


if __name__ == "__main__":
    main()
