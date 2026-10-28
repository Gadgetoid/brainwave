# Hardware

## Power

The Brain attaches only when it sees voltage on VBUS. With VSYS bridged to VBUS the battery voltage (about 3.9 V) is enough: the USB PHY's own B-session comparator accepts it, although the power block's VBUSVALID threshold (`HW_POWER_5VCTRL.VBUSVALID_TRSH`) is set to 4.0 V by Sharp's CE. A flaky bridge drops the link the moment it opens.

While VBUS is present the i.MX28 runs itself from its 4.2 V rail (`DCDC4P2`) and charges its battery, so it draws heavily on Brainwave's LiPo. `bwpower.dll`, a stream driver the app loads from the card, performs NXP's 5 V to battery handoff (clears `DCDC4P2.ENABLE_DCDC` and `ENABLE_4P2`, sets `5VCTRL.PWD_CHARGE_4P2` and `CHARGE.PWD_BATTCHRG`). Tested on a laptop's 5 V: no reset, the battery voltage drops under load as expected, and Sharp's driver leaves it alone until the next plug event (it only enables VBUSVALID, VDD5V droop and power switch interrupts). The app re-applies it every 2 s.

The driver can also lower `VBUSVALID_TRSH` to 2.9 V and route it to the USB B-session (`VBUSVALID_TO_B`), so USB keeps working down to LiPo cut-off. This is off by default and untested.

## USB

- Native RP2350 USB in host mode, full speed. TinyUSB 0.21 is pinned separately from pico-sdk 2.3.0, whose 0.18 polls bulk endpoints once per frame (about 64 KB/s).
- The Brain's RNDIS function (EVK `rndisfn.dll`, `045e:0302`): interface 0 is `02/02/FF` with an interrupt IN endpoint and no IAD, interface 1 is `0A` with bulk IN and OUT.
- Sharp's mass storage mode exposes only the Brain's internal user storage, not the card, so the module cannot install anything itself.
- After a suspend the RNDIS client is dead; the app selects storage and then RNDIS again on resume.

## CYW43

- The status LED is on the CYW43. An LED ioctl during a WiFi join can block indefinitely, so the LED is left alone while joining.
- In poll mode, packets arriving over WiFi are handled inside `cyw43_arch_poll`. CYW43 calls made from there (RSSI, BSSID, scan, join) deadlock, so the control service only records requests and `wifi::task` carries them out from the main loop.

## Debugging

- UART0 (GP0 TX, 115200) carries the log.
- The log ring is in uninitialised RAM at the address of `persistent` in the ELF (0x20000110 in current builds) and survives watchdog and fault reboots. With an SWD probe: `dump_image ring.bin 0x20000110 4108` without halting.
- Flashing slot A over SWD without touching the partition table: `program build/firmware/brainwave.bin 0x10002000 verify reset exit`. Use `reset run` rather than `halt` then `resume`, which leaves the core stopped; a halt of a few seconds also drops WiFi.
- `-DBRAINWAVE_DEBUG_FORWARD=ON` forwards TCP 2100 on the module's WiFi address to the Brain's Debug Agent and opens the control port on WiFi. `-DBRAINWAVE_TEST_HANG_AFTER_MS=10000` builds an image that hangs on purpose, to test rollback.
