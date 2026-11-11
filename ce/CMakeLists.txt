cmake_minimum_required(VERSION 3.20)
project(brainwave_ce LANGUAGES C CXX)

include(Grimace)

set(BRAINWAVE_ICON "${CMAKE_BINARY_DIR}/brainwave.ico")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/make-icon.py" "${BRAINWAVE_ICON}"
    COMMAND_ERROR_IS_FATAL ANY)

grimace_add_executable(brainwave ICON "${BRAINWAVE_ICON}"
    src/main.cpp src/app.cpp src/usb_activation.cpp src/power_policy.cpp src/update_flow.cpp src/usb_client.cpp src/module.cpp src/text.cpp src/power.cpp src/resume.cpp src/worker.cpp src/model.cpp
    src/settings.cpp src/tray.cpp src/ui_style.cpp src/ui_networks.cpp src/ui_details.cpp src/ui_advanced.cpp src/ui_password.cpp src/sha256.cpp src/firmware_update.cpp)
target_compile_options(brainwave PRIVATE -mcpu=arm926ej-s -Wall -Werror -Wno-unused-parameter)
target_compile_definitions(brainwave PRIVATE UNICODE _UNICODE UNDER_CE WINCE _WINDOWS GRIMACE_ALL_DECLARATIONS)
target_link_libraries(brainwave PRIVATE grimace::commctrl grimace::winsock)

grimace_add_library(bwpower driver/brainwave_power.c
    OUTPUT bwpower.dll EXPORTS BWP_Init BWP_Deinit BWP_Open BWP_Close BWP_IOControl)
target_compile_options(bwpower PRIVATE -mcpu=arm926ej-s -std=c11 -Wall -Werror -Wno-unused-parameter)
target_compile_definitions(bwpower PRIVATE UNICODE _UNICODE UNDER_CE WINCE _WINDOWS GRIMACE_ALL_DECLARATIONS)
