# Control Protocol

The module listens on TCP port 2350 at `10.88.0.1`, its address on the USB link and the Brain's default gateway.

One command per connection: send a line ending in `\n`, then read lines up to `ok` or `error MESSAGE`. Stop reading there; do not wait for the connection to close.

SSIDs and passwords are lowercase hex of their bytes. An empty value is `-`.

## Commands

### `status`

```
version=0.3.4
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
- `debug-forward`: `on` while `debug-forward` is on.
- `slot`: the firmware slot running, `A` or `B` (`unpartitioned` without a partition table).

Clients ignore keys they do not know.

### `scan`

Takes about 3 s. One line per SSID, strongest first, hidden networks left out:

```
ap -48 wpa2 6 1 486f6d65
ap -71 open 11 0 436166c3a9
ok
```

Fields: signal in dBm, security (`open`, `wep`, `wpa`, `wpa2`), channel, saved (`1` or `0`), SSID.

### `join SSID PASSWORD`

Saves the network and starts connecting; `PASSWORD` is `-` for an open network. Up to 8 networks are kept, the least recently used dropped first. Returns `ok` at once; poll `status` for the result.

### `connect SSID`

Connects to a saved network, or answers `error not saved`.

### `disconnect`

Leaves the current network and stays off WiFi until `join` or `connect`, or the module restarts.

### `debug-forward on|off`

Forwards TCP port 2100 on the module's WiFi address to the Brain's Debug Agent. Off at start unless built with `BRAINWAVE_DEBUG_FORWARD`. While on, anyone on the WiFi network can write files to the Brain and run programs on it.

### `update SIZE SHA256`

Writes `brainwave.bin` (SIZE bytes, SHA256 in hex) into the slot that is not running. The module answers `ready`, the client sends the SIZE bytes on the same connection, and the module answers `ok` once the image is written and verified in flash. 3 s later it restarts into the new image on trial and keeps it after 30 s of running; a hang or crash before then puts the previous image back. `tools/push-firmware.py` is a reference client.

### `forget SSID`

Removes a saved network, disconnecting if it is the current one.

### `known`

Saved networks, most recently used first:

```
net 486f6d65
ok
```

### `log`

The last 4 KB of the module's log, oldest first.
