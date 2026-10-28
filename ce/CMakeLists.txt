cmake_minimum_required(VERSION 3.20)
project(brainwave_ce LANGUAGES C CXX)

if(NOT JORNADA_TOOLCHAIN)
    message(FATAL_ERROR "Set JORNADA_TOOLCHAIN to the jornada-72x-tools directory")
endif()

find_program(NODE_EXECUTABLE node REQUIRED)
list(APPEND CMAKE_MODULE_PATH "${JORNADA_TOOLCHAIN}/cmake")
include(JornadaCE)

set(BRAIN_IMPORT_DIRECTORY "${CMAKE_BINARY_DIR}/brain-imports")
execute_process(
    COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/prepare-imports.mjs"
        "${CMAKE_CURRENT_LIST_DIR}/ce-imports" "${BRAIN_IMPORT_DIRECTORY}"
    COMMAND_ERROR_IS_FATAL ANY)
set(coredll_stubs "${BRAIN_IMPORT_DIRECTORY}/coredll.S")
add_custom_command(OUTPUT "${coredll_stubs}"
    COMMAND "${Python3_EXECUTABLE}" "${JORNADA_TOOLCHAIN}/tools/mkimplib.py"
        "${BRAIN_IMPORT_DIRECTORY}/coredll.txt" "coredll.dll" "${coredll_stubs}"
    DEPENDS "${BRAIN_IMPORT_DIRECTORY}/coredll.txt"
    VERBATIM)
add_library(brain_import_coredll STATIC "${coredll_stubs}")
set(commctrl_stubs "${BRAIN_IMPORT_DIRECTORY}/commctrl.S")
add_custom_command(OUTPUT "${commctrl_stubs}"
    COMMAND "${Python3_EXECUTABLE}" "${JORNADA_TOOLCHAIN}/tools/mkimplib.py"
        "${BRAIN_IMPORT_DIRECTORY}/commctrl.txt" "commctrl.dll" "${commctrl_stubs}"
    DEPENDS "${BRAIN_IMPORT_DIRECTORY}/commctrl.txt"
    VERBATIM)
add_library(brain_import_commctrl STATIC "${commctrl_stubs}")

add_executable(brainwave src/main.cpp src/app.cpp src/usb_activation.cpp src/power_policy.cpp src/update_flow.cpp src/usb_client.cpp src/module.cpp src/text.cpp src/power.cpp src/resume.cpp src/worker.cpp src/model.cpp
    src/settings.cpp src/tray.cpp src/ui_style.cpp src/ui_networks.cpp src/ui_details.cpp src/ui_advanced.cpp src/ui_password.cpp src/sha256.cpp src/firmware_update.cpp)
set_target_properties(brainwave PROPERTIES SUFFIX ".elf" LINKER_LANGUAGE CXX)
target_compile_options(brainwave PRIVATE -mcpu=arm926ej-s -Wall -Werror -Wno-unused-parameter)
target_compile_definitions(brainwave PRIVATE UNICODE _UNICODE UNDER_CE WINCE _WINDOWS JORNADA_ALL_DECLARATIONS)
target_link_options(brainwave PRIVATE -T "${CMAKE_CURRENT_LIST_DIR}/exe.ld" --entry=__jornada_exe_start)
set_property(TARGET brainwave APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/exe.ld")
target_link_libraries(brainwave PRIVATE jornada::headers jornada::runtime jornada::cxx_runtime jornada::winsock brain_import_commctrl brain_import_coredll)
add_custom_command(TARGET brainwave POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${JORNADA_TOOLCHAIN}/tools/mkpe.py"
        "$<TARGET_FILE:brainwave>" "${CMAKE_BINARY_DIR}/brainwave.exe" --ce-version=5.1
    VERBATIM)

add_executable(brainwave_power driver/brainwave_power.c)
set_target_properties(brainwave_power PROPERTIES SUFFIX ".elf")
target_compile_options(brainwave_power PRIVATE -mcpu=arm926ej-s -std=c11 -Wall -Werror -Wno-unused-parameter)
target_compile_definitions(brainwave_power PRIVATE UNICODE _UNICODE UNDER_CE WINCE _WINDOWS JORNADA_ALL_DECLARATIONS)
target_link_options(brainwave_power PRIVATE -T "${CMAKE_CURRENT_LIST_DIR}/dll.ld" --entry=DllMain
    --undefined=BWP_Init --undefined=BWP_Deinit --undefined=BWP_Open --undefined=BWP_Close --undefined=BWP_IOControl)
set_property(TARGET brainwave_power APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/dll.ld")
target_link_libraries(brainwave_power PRIVATE jornada::headers jornada::runtime brain_import_coredll)
add_custom_command(TARGET brainwave_power POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${JORNADA_TOOLCHAIN}/tools/mkpe.py"
        "$<TARGET_FILE:brainwave_power>" "${CMAKE_BINARY_DIR}/bwpower.dll" --ce-version=5.1
        --exports=BWP_Init,BWP_Deinit,BWP_Open,BWP_Close,BWP_IOControl
    VERBATIM)
