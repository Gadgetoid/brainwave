# Control Protocol

The Brainwave app on the Brain configures the module over the USB network link. The module listens on TCP port 2350 at its own address on that link, `10.88.0.1`, which is also the Brain's default gateway.

One command per connection: the client connects, sends one line ending in `\n`, and reads lines until the last line, which is `ok` or `error MESSAGE`. Clients stop reading at that line; they do not wait for the connection to close.

SSIDs and passwords are sent as lowercase hex of their bytes (SSIDs are usually UTF-8), so they need no quoting. An empty value is `-`.

## Commands

### `status`

```
version=0.2.0
wifi=connected
ssid=486f6d65
security=wpa2
channel=6
rssi=-58
bssid=a0:b1:c2:d3:e4:f5
ip=192.168.1.42
netmask=255.255.255.0
gateway=192.168.1.1
dns=192.168.1.1
brain=10.88.0.2
mac=28:cd:c1:00:00:01
battery=3912
uptime=5321
known=2
debug-forward=off
slot=A
ok
```

- `wifi`: `idle` (no saved network), `scanning`, `connecting`, `connected`, `failed` (wrong password or no answer), `not-found` (no saved network in range), `disconnected` (by `disconnect`; no automatic reconnection until `join` or `connect`).
- `ssid`: the network being joined (`connecting`, `failed`) or joined (`connected`), `-` otherwise.
- `security`, `channel`: from the last scan that saw that network, `-` if unknown.
- `rssi` (dBm), `bssid`, `ip`, `netmask`, `gateway`, `dns`: `-` unless connected.
- `brain`: the address leased to the Brain, `-` before its first lease.
- `mac`: the module's WiFi MAC address.
- `battery`: the module's battery (VBAT) in millivolts.
- `uptime`: seconds since the module started.
- `debug-forward`: `on` when TCP port 2100 on the module's WiFi address is forwarded to the Brain (Debug Agent).
- `slot`: the firmware slot running, `A` or `B` (`unpartitioned` without a partition table).

Clients ignore keys they do not know.

### `scan`

Scans (about 3 s) and lists one line per network, strongest first, one entry per SSID, hidden networks left out:

```
ap -48 wpa2 6 1 486f6d65
ap -71 open 11 0 436166c3a9
ok
```

Fields: signal in dBm, security (`open`, `wep`, `wpa`, `wpa2`), channel, saved (`1` if a password or open network is saved for it), SSID.

### `join SSID PASSWORD`

Saves the network with its password (replacing a saved one with the same SSID, at most 8 kept, the least recently used dropped) and starts connecting. `PASSWORD` is `-` for an open network. Returns `ok` at once; poll `status` for the result.

### `connect SSID`

Connects to a saved network with its saved password. `error not saved` if it is not saved.

### `disconnect`

Leaves the current network and stays off WiFi until `join` or `connect`, or the module restarts.

### `debug-forward on|off`

Forwards TCP port 2100 on the module's WiFi address to the Brain's port 2100, so `brain-debug --device MODULE_IP` reaches Debug Agent (Network) from the WiFi network. Off at start unless the firmware was built with `BRAINWAVE_DEBUG_FORWARD`. Anyone on the WiFi network can then write files to the Brain and run programs on it.

### `update SIZE SHA256`

Writes a new firmware image (`brainwave.bin`, SIZE bytes, SHA256 in hex) into the slot that is not running. The module answers `ready` (or `error ...`), the client then sends exactly SIZE bytes on the same connection, and the module answers `ok` once the image is written and its hash verified in flash, or `error ...`. About 1.5 s after `ok` it restarts into the new image on trial; the new firmware keeps itself after 30 s of running. If it hangs or crashes before then, the watchdog restarts it and the boot ROM goes back to the previous image. `tools/push-firmware.py` does this from a computer.

### `forget SSID`

Removes a saved network and disconnects if it is the current one.

### `known`

Saved networks, most recently used first:

```
net 486f6d65
ok
```

### `log`

The module's recent log lines (a 4 KB ring), oldest first, then `ok`.
