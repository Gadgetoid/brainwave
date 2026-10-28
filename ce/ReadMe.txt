Brainwave WiFi
==============

Brainwave is a USB WiFi module for the SHARP Brain (PW-SH1, PW-SH2) running a pwsh2-boot card.

Using it
--------

1. Start > Programs > System > Brainwave WiFi. The first time, it switches the Brain's USB to networking and asks you to plug Brainwave in again.
2. Pick a network on the Networks tab and tap Connect. Brainwave asks for the password once and remembers up to 8 networks, joining the strongest one it knows when it starts.
3. Tap the X to hide the app. It stays in the tray with a signal icon and restarts the USB link after the Brain resumes from suspend.

The light
---------

- Solid: connected to the Brain and to WiFi.
- Blinking once a second: connected to the Brain, no WiFi.
- Short blink every two seconds: WiFi only, or nothing.

Notes
-----

- Brainwave gives the Brain the address 10.88.0.2; Brainwave itself is 10.88.0.1.
- Advanced > Brain power keeps the Brain running from its own battery while Brainwave is attached, so it does not drain Brainwave's battery.
- Advanced > Switch USB to Storage switches the Brain's USB back to mass storage.
- Saved WiFi passwords are stored unencrypted in Brainwave's flash.
