cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "$ENV{HOME}/.pico-sdk/sdk/2.2.0")
endif()
if(NOT DEFINED PICO_TOOLCHAIN_PATH AND NOT DEFINED ENV{PICO_TOOLCHAIN_PATH})
    file(GLOB _tc "$ENV{HOME}/.pico-sdk/toolchain/arm-gnu-toolchain-*")
    if(_tc)
        list(GET _tc 0 PICO_TOOLCHAIN_PATH)
    endif()
endif()
set(PICO_BOARD pico2_w CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

include(pico_sdk_import.cmake)

set(PICOCALC_VNC_VERSION "1.0.0" CACHE STRING "Firmware release version")
project(picocalc_vnc VERSION ${PICOCALC_VNC_VERSION} LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(picocalc_vnc
    src/main.c
    src/lcd.c
    src/gfx.c
    src/kbd.c
    src/ui.c
    src/help.c
    src/font_6x12.c
    src/font_8x16.c
    src/sdcard.c
    src/diskio.c
    src/config.c
    src/wifi.c
    src/net.c
    src/des.c
    src/vnc.c
    src/https.c
    src/update.c
    src/update_format.c
    third_party/fatfs/ff.c
    third_party/fatfs/ffunicode.c
    third_party/miniz/miniz_tinfl.c
)

target_include_directories(picocalc_vnc PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/third_party/fatfs
    ${CMAKE_CURRENT_LIST_DIR}/third_party/miniz
)

target_compile_definitions(picocalc_vnc PRIVATE
    CYW43_HOST_NAME=\"PicoCalc-VNC\"
    PICOCALC_VNC_VERSION=\"${PROJECT_VERSION}\"
    PICO_STACK_SIZE=0x1000
    PICO_CORE1_STACK_SIZE=0x800
)

target_compile_options(picocalc_vnc PRIVATE -O2 -Wall -Wno-unused-function -Wno-format-truncation)
set_source_files_properties(
    third_party/fatfs/ff.c third_party/fatfs/ffunicode.c third_party/miniz/miniz_tinfl.c
    PROPERTIES COMPILE_OPTIONS "-w")

target_link_libraries(picocalc_vnc
    pico_stdlib
    pico_multicore
    pico_cyw43_arch_lwip_poll
    hardware_spi
    hardware_i2c
    hardware_watchdog
    pico_mbedtls
)

file(READ "${CMAKE_CURRENT_LIST_DIR}/src/update-ca.pem" UPDATE_CA)
string(REPLACE "\n" "\\n\"\n\"" UPDATE_CA "${UPDATE_CA}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/update_ca.h"
    CONTENT "static const char update_ca[] = \"@UPDATE_CA@\";\n" @ONLY)
target_include_directories(picocalc_vnc PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

pico_set_program_name(picocalc_vnc "PicoCalc VNC")
pico_set_program_description(picocalc_vnc "VNC client for the ClockworkPi PicoCalc")
pico_set_program_version(picocalc_vnc "${PROJECT_VERSION}")

pico_enable_stdio_uart(picocalc_vnc 1)
pico_enable_stdio_usb(picocalc_vnc 0)

pico_add_extra_outputs(picocalc_vnc)
