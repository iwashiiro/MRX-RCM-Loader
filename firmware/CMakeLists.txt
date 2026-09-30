cmake_minimum_required(VERSION 3.13...3.27)

if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH is not set. Point it to your Raspberry Pi Pico SDK checkout.")
endif()

include(pico_sdk_import.cmake)

set(PICO_PIO_USB_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/pico-pio-usb" CACHE PATH
    "Path to the Pico-PIO-USB dependency")

project(mrx_loader_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(mrx_loader
    src/main.c
    src/mrx_protocol.c
    src/config.c
    src/storage.c
    src/rp2040_flash_bd.c
    src/payload_manager.c
    src/mrx_sha256.c
    src/usb_descriptors.c
    src/usb_host.c
    src/mrx_loader.c
    src/mrx_led.c
    src/rcm.cpp
    third_party/littlefs/lfs.c
    third_party/littlefs/lfs_util.c
)

pico_generate_pio_header(mrx_loader ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

set(MRX_INTERMEZZO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/rcm")
file(MAKE_DIRECTORY "${MRX_INTERMEZZO_DIR}")
set(MRX_INTERMEZZO_OBJECT "${MRX_INTERMEZZO_DIR}/mrx_intermezzo_blob.o")
add_custom_command(
    OUTPUT "${MRX_INTERMEZZO_OBJECT}"
    COMMAND "${CMAKE_C_COMPILER}" -mcpu=arm7tdmi -marm -mlittle-endian -Os
        -ffreestanding -fno-builtin
        -DSTART_OF_IRAM=0x40000000
        -DRELOCATION_TARGET=0x40010000
        -DLOAD_BLOCK_START=0x40020000
        -c "${CMAKE_CURRENT_LIST_DIR}/src/rcm_intermezzo.S"
        -o "${MRX_INTERMEZZO_DIR}/mrx_intermezzo.o"
    COMMAND "${CMAKE_LINKER}" -T "${CMAKE_CURRENT_LIST_DIR}/src/rcm_intermezzo.ld"
        --defsym LOAD_ADDR=0x4001F000
        "${MRX_INTERMEZZO_DIR}/mrx_intermezzo.o"
        -o "${MRX_INTERMEZZO_DIR}/mrx_intermezzo.elf"
    COMMAND "${CMAKE_OBJCOPY}" -O binary
        "${MRX_INTERMEZZO_DIR}/mrx_intermezzo.elf"
        "${MRX_INTERMEZZO_DIR}/mrx_intermezzo.bin"
    COMMAND "${CMAKE_OBJCOPY}" -I binary -O elf32-littlearm -B arm
        mrx_intermezzo.bin mrx_intermezzo_blob.o
    DEPENDS src/rcm_intermezzo.S src/rcm_intermezzo.ld
    WORKING_DIRECTORY "${MRX_INTERMEZZO_DIR}"
    VERBATIM
)
add_custom_target(mrx_intermezzo_blob DEPENDS "${MRX_INTERMEZZO_OBJECT}")
add_dependencies(mrx_loader mrx_intermezzo_blob)
target_sources(mrx_loader PRIVATE "${MRX_INTERMEZZO_OBJECT}")
target_link_options(mrx_loader PRIVATE "LINKER:--undefined=mrx_rcm_inject_selected")

target_include_directories(mrx_loader PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/third_party/littlefs
)

target_link_libraries(mrx_loader
    pico_stdlib
    pico_unique_id
    pico_flash
    hardware_flash
    tinyusb_device
    tinyusb_host
    tinyusb_pico_pio_usb
    pico_multicore
    hardware_clocks
    hardware_pio
)

pico_enable_stdio_usb(mrx_loader 0)
pico_enable_stdio_uart(mrx_loader 0)

if(NOT COMMAND pico_add_linker_script_override_path)
    message(FATAL_ERROR "This Pico SDK lacks pico_add_linker_script_override_path; use an SDK version that supports linker script overrides.")
endif()

function(mrx_limit_firmware_flash target)
    set(PICO_FLASH_SIZE_BYTES_STRING "1 * 1024 * 1024")
    set(mrx_flash_region_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}_flash_region")
    file(MAKE_DIRECTORY "${mrx_flash_region_dir}")
    configure_file(
        "${PICO_SDK_PATH}/src/rp2_common/pico_standard_link/pico_flash_region.template.ld"
        "${mrx_flash_region_dir}/pico_flash_region.ld"
    )
    pico_add_linker_script_override_path(
        ${target}
        "${mrx_flash_region_dir}"
        FILES pico_flash_region.ld
    )
endfunction()

mrx_limit_firmware_flash(mrx_loader)

pico_add_extra_outputs(mrx_loader)

pico_set_program_name(mrx_loader "MRX Loader")
pico_set_program_version(mrx_loader "0.1.0")
pico_set_program_description(mrx_loader "MRX Loader firmware")
