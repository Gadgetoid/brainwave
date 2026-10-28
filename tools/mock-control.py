"""A fake Brainwave control service for testing the app on a Brain through brain-net."""

#!/usr/bin/env python3
import argparse
import random
import socketserver
import threading
import time

VERSION = "0.2.0-mock"
NETWORKS = [
    (-48, "wpa2", 6, "Home", "a0:b1:c2:d3:e4:01"),
    (-63, "wpa2", 1, "Neighbour 5G", "a0:b1:c2:d3:e4:02"),
    (-71, "open", 11, "Café", "a0:b1:c2:d3:e4:03"),
    (-80, "wpa", 3, "Old Router", "a0:b1:c2:d3:e4:04"),
]
PASSWORDS = {"Home": "correct horse", "Neighbour 5G": "secret12", "Old Router": "letmein1"}
KNOWN_LIMIT = 8
STARTED = time.time()

lock = threading.RLock()
state = {"wifi": "idle", "ssid": None, "since": 0.0, "password_ok": False, "rssi": -55.0, "debug_forward": False}
known = []
log_lines = []


def log(text):
    with lock:
        log_lines.append(time.strftime("%H:%M:%S ") + text)
        del log_lines[:-64]


def to_hex(text):
    return text.encode("utf-8").hex() if text else "-"


def from_hex(value):
    return "" if value == "-" else bytes.fromhex(value).decode("utf-8")


def network(ssid):
    return next((entry for entry in NETWORKS if entry[3] == ssid), None)


def saved(ssid):
    return any(name == ssid for name, _ in known)


def remember(ssid, password):
    known[:] = [(name, secret) for name, secret in known if name != ssid]
    known.insert(0, (ssid, password))
    del known[KNOWN_LIMIT:]


def start_connecting(ssid, password):
    entry = network(ssid)
    state.update(wifi="connecting", ssid=ssid, since=time.time(),
                 password_ok=entry is not None and (entry[1] == "open" or PASSWORDS.get(ssid) == password))
    if entry:
        state["rssi"] = float(entry[0])
    log(f"connecting to {ssid}")


def advance():
    if state["wifi"] == "connecting" and time.time() - state["since"] >= 2:
        state["wifi"] = "connected" if state["password_ok"] else "failed"
        log(f"{state['wifi']} {state['ssid']}")
    if state["wifi"] == "connected":
        entry = network(state["ssid"])
        base = entry[0] if entry else -60
        state["rssi"] += random.uniform(-3, 3) + (base - state["rssi"]) * 0.1
        state["rssi"] = max(-95.0, min(-30.0, state["rssi"]))


def battery_millivolts():
    return int(4150 - (time.time() - STARTED) * 0.5)


def status_lines():
    with lock:
        advance()
        connected = state["wifi"] == "connected"
        ssid = state["ssid"] if state["wifi"] in ("connecting", "connected", "failed") else None
        entry = network(ssid) if ssid else None
        return [
            f"version={VERSION}",
            f"wifi={state['wifi']}",
            f"ssid={to_hex(ssid)}",
            f"security={entry[1] if entry else '-'}",
            f"channel={entry[2] if entry else '-'}",
            f"rssi={int(state['rssi']) if connected else '-'}",
            f"bssid={entry[4] if entry and connected else '-'}",
            f"ip={'192.168.1.42' if connected else '-'}",
            f"netmask={'255.255.255.0' if connected else '-'}",
            f"gateway={'192.168.1.1' if connected else '-'}",
            f"dns={'192.168.1.1' if connected else '-'}",
            "brain=10.0.2.15",
            "mac=28:cd:c1:00:00:01",
            f"battery={battery_millivolts()}",
            f"uptime={int(time.time() - STARTED)}",
            f"debug-forward={'on' if state['debug_forward'] else 'off'}",
            f"known={len(known)}",
            "ok",
        ]


def handle(command):
    words = command.split()
    if not words:
        return ["error empty command"]
    name, arguments = words[0], words[1:]
    try:
        if name == "status" and not arguments:
            return status_lines()
        if name == "scan" and not arguments:
            time.sleep(3)
            with lock:
                lines = [f"ap {rssi + random.randint(-4, 4)} {security} {channel} {1 if saved(ssid) else 0} {to_hex(ssid)}"
                         for rssi, security, channel, ssid, _ in NETWORKS]
            return lines + ["ok"]
        if name == "join" and len(arguments) == 2:
            ssid, password = from_hex(arguments[0]), from_hex(arguments[1])
            if not ssid:
                return ["error bad network or password"]
            with lock:
                remember(ssid, password)
                start_connecting(ssid, password)
            return ["ok"]
        if name == "connect" and len(arguments) == 1:
            ssid = from_hex(arguments[0])
            with lock:
                entry = next(((name, secret) for name, secret in known if name == ssid), None)
                if not entry:
                    return ["error not saved"]
                remember(*entry)
                start_connecting(*entry)
            return ["ok"]
        if name == "disconnect" and not arguments:
            with lock:
                state.update(wifi="disconnected", ssid=None)
            log("disconnected")
            return ["ok"]
        if name == "debug-forward" and len(arguments) == 1 and arguments[0] in ("on", "off"):
            with lock:
                state["debug_forward"] = arguments[0] == "on"
            log(f"debug-forward {arguments[0]}")
            return ["ok"]
        if name == "forget" and len(arguments) == 1:
            ssid = from_hex(arguments[0])
            with lock:
                if not saved(ssid):
                    return ["error not saved"]
                known[:] = [(name, secret) for name, secret in known if name != ssid]
                if state["ssid"] == ssid:
                    state.update(wifi="not-found" if known else "idle", ssid=None)
            log(f"forget {ssid}")
            return ["ok"]
        if name == "known" and not arguments:
            with lock:
                return [f"net {to_hex(ssid)}" for ssid, _ in known] + ["ok"]
        if name == "log" and not arguments:
            with lock:
                return list(log_lines) + ["ok"]
    except (ValueError, UnicodeDecodeError):
        return ["error bad hex"]
    return ["error unknown command"]


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        line = self.rfile.readline(1024).decode("ascii", "replace").strip()
        print(f"{self.client_address[0]}: {line.split(' ')[0] if line.startswith('join') else line}", flush=True)
        self.wfile.write("".join(reply + "\n" for reply in handle(line)).encode("utf-8"))


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser(description="Fake Brainwave control service for testing the CE app through brain-net.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2350)
    options = parser.parse_args()
    with Server((options.host, options.port), Handler) as server:
        print(f"mock-control: listening on {options.host}:{options.port}", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
