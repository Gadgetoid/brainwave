# Brainwave firmware for the Pimoroni Pico LiPo 2XL W.
cmake_minimum_required(VERSION 3.20)

set(PICO_BOARD pimoroni_pico_lipo2xl_w)
set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards")
set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk")
set(PICO_TINYUSB_PATH "${CMAKE_CURRENT_LIST_DIR}/tinyusb")

include(pico_sdk_import.cmake)
project(brainwave C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

set(BRAINWAVE_VERSION "0.3.4")
set(BRAINWAVE_TEST_HANG_AFTER_MS 0 CACHE STRING "Test only: stop feeding the watchdog after this many milliseconds (0 is off)")
option(BRAINWAVE_DEBUG_FORWARD "Forward TCP 2100 on the WiFi address to the Brain's Debug Agent from boot" OFF)

add_executable(brainwave
    src/main.cpp
    src/log.cpp
    src/battery.cpp
    src/crash.cpp
    src/status_led.cpp
    src/rndis_host.cpp
    src/brain_link.cpp
    src/napt.cpp
    src/napt_lwip.cpp
    src/dhcp_server.cpp
    src/dns_relay.cpp
    src/control.cpp
    src/wifi.cpp
    src/settings.cpp
    src/updater.cpp
)

target_include_directories(brainwave PRIVATE src include)
target_compile_definitions(brainwave PRIVATE
    BRAINWAVE_VERSION="${BRAINWAVE_VERSION}"
    BRAINWAVE_DEBUG_FORWARD=$<BOOL:${BRAINWAVE_DEBUG_FORWARD}>
    BRAINWAVE_TEST_HANG_AFTER_MS=${BRAINWAVE_TEST_HANG_AFTER_MS}
    CYW43_LWIP=1
    PICO_PANIC_FUNCTION=brainwave_panic
    PICO_PANIC_FUNCTION_DOES_NOT_RETURN=1
    PICO_CRT0_IMAGE_TYPE_TBYB=1
)
target_compile_options(brainwave PRIVATE -Wall -Wextra -Wno-unused-parameter
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)

target_link_libraries(brainwave
    pico_stdlib
    pico_cyw43_arch_lwip_poll
    pico_flash
    hardware_flash
    hardware_adc
    pico_sha256
    boot_uf2_headers
    boot_picoboot_headers
    tinyusb_host
    tinyusb_board
)

pico_enable_stdio_uart(brainwave 1)
pico_enable_stdio_usb(brainwave 0)
pico_add_extra_outputs(brainwave)
