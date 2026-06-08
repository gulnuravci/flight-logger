cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(flight_logger C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Pull LittleFS from upstream
include(FetchContent)
FetchContent_Declare(
    littlefs
    GIT_REPOSITORY https://github.com/littlefs-project/littlefs.git
    GIT_TAG        v2.9.3
)
FetchContent_MakeAvailable(littlefs)

# Pull FatFs from upstream; replace its template ffconf.h with ours.
FetchContent_Declare(
    fatfs
    GIT_REPOSITORY https://github.com/abbrev/fatfs.git
    GIT_TAG        R0.15
)
FetchContent_GetProperties(fatfs)
if(NOT fatfs_POPULATED)
    FetchContent_Populate(fatfs)
    configure_file(
        ${CMAKE_SOURCE_DIR}/src/storage/ffconf.h
        ${fatfs_SOURCE_DIR}/source/ffconf.h
        COPYONLY
    )
endif()

pico_sdk_init()

add_executable(flight_logger
    src/main.c
    # Sensors
    src/sensors/lsm6dsox.c
    src/sensors/bmp390.c
    # Storage
    src/storage/flash_fs.c
    src/storage/sd_spi.c
    src/storage/diskio.c
    src/storage/logger.c
    # Network
    src/network/credentials.c
    src/network/wifi.c
    src/network/wx_fetch.c
    src/network/dhcpserver.c
    src/network/httpserver.c
    # LittleFS sources
    ${littlefs_SOURCE_DIR}/lfs.c
    ${littlefs_SOURCE_DIR}/lfs_util.c
    # FatFs core
    ${fatfs_SOURCE_DIR}/source/ff.c
)

# All subdirs are listed so headers can be included by basename (e.g. "bmp390.h")
# lwipopts.h must be on the include path for lwIP to find it — src/network/ covers that
target_include_directories(flight_logger PRIVATE
    src
    src/sensors
    src/storage
    src/network
    ${littlefs_SOURCE_DIR}
    ${fatfs_SOURCE_DIR}/source
)

pico_set_program_name(flight_logger "flight_logger")
pico_set_program_version(flight_logger "0.1")

pico_enable_stdio_usb(flight_logger 1)
pico_enable_stdio_uart(flight_logger 0)

target_link_libraries(flight_logger
    pico_stdlib
    # WiFi with lwIP networking stack (replaces pico_cyw43_arch_none)
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_i2c
    hardware_spi        # SD card SPI bus
    hardware_flash
    pico_flash
    pico_float
    pico_double
    pico_lwip_http      # includes lwIP HTTP client (httpc)
)

pico_add_extra_outputs(flight_logger)
