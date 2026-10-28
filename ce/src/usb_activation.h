// Puts the Brain's USB port into RNDIS networking for the module, or back to mass storage.
#pragma once

void usb_install_rndis_driver();
void usb_activate_rndis();
bool usb_restore_storage();
