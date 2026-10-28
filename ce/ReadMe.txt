Brainwave WiFi
==============

Brainwave is a USB WiFi module for the SHARP Brain (PW-SH1, PW-SH2) running a pwsh2-boot card.

Using it
--------

1. Start > Programs > Brainwave WiFi. The first time, it switches the Brain's USB to networking and asks you to plug Brainwave in again.
2. Pick a network on the Networks tab and tap Connect. Brainwave asks for the password once, remembers up to 8 networks and joins the strongest one it knows when it starts.
3. The X hides the app to the tray. It restarts the USB link after the Brain resumes from suspend.
4. To update Brainwave, put a new brainwave.bin in this folder and tap Advanced > Update Module Firmware.

The light
---------

- Solid: connected to the Brain and to WiFi.
- Blinking once a second: connected to the Brain, no WiFi.
- Short blink every two seconds: WiFi only, or nothing.

Notes
-----

- The Brain gets 10.88.0.2; Brainwave is 10.88.0.1.
- Advanced > Brain power runs the Brain from its own battery while Brainwave is attached.
- Advanced > Switch USB to Storage switches the Brain's USB back to mass storage.
- Saved WiFi passwords are stored unencrypted in Brainwave's flash.
