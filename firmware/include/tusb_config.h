// TinyUSB configuration: host only, one device, no built-in class drivers.
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_MCU OPT_MCU_RP2040
#define CFG_TUSB_OS OPT_OS_PICO
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_HOST
#define CFG_TUSB_DEBUG 0
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 0
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_HUB 0
#define CFG_TUH_DEVICE_MAX 1
#define CFG_TUH_INTERFACE_MAX 4
#define CFG_TUH_ENDPOINT_MAX 8

#define CFG_TUH_MSC 0
#define CFG_TUH_CDC 0
#define CFG_TUH_HID 0
#define CFG_TUH_VENDOR 0

#endif
