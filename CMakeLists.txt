# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_BUILD_TYPE "Release")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
# set(PICO_BOARD pico CACHE STRING "Board type")
set(PICO_BOARD pico2_w)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(pico-wspr-tx C CXX ASM)

pico_sdk_init()
add_executable(pico-wspr-tx)
pico_generate_pio_header(pico-wspr-tx ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/piodco/dco2.pio)

# Not only does the WiFi interface use PIO1 by default, but it also uses 2 X DMA to tx / rx the SPI data.
target_compile_definitions(pico-wspr-tx PRIVATE
    CYW43_SPI_PIO_PREFERRED_PIO=0
)

target_sources(pico-wspr-tx PUBLIC
               ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/lib/assert.c
               ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/piodco/piodco.c
               ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/gpstime/GPStime.c
               ${CMAKE_CURRENT_LIST_DIR}/TxChannel/TxChannel.c
               ${CMAKE_CURRENT_LIST_DIR}/WSPRbeacon/thirdparty/WSPRutility.c
               ${CMAKE_CURRENT_LIST_DIR}/WSPRbeacon/thirdparty/nhash.c
               ${CMAKE_CURRENT_LIST_DIR}/WSPRbeacon/thirdparty/maidenhead.c
               ${CMAKE_CURRENT_LIST_DIR}/WSPRbeacon/WSPRbeacon.c
               ${CMAKE_CURRENT_LIST_DIR}/debug/logutils.c
               ${CMAKE_CURRENT_LIST_DIR}/init.c
               ${CMAKE_CURRENT_LIST_DIR}/core1.c
               ${CMAKE_CURRENT_LIST_DIR}/main.c
               ${CMAKE_CURRENT_LIST_DIR}/ft8/encode.c
               ${CMAKE_CURRENT_LIST_DIR}/ft8/message.c
               ${CMAKE_CURRENT_LIST_DIR}/ft8/text.c
               ${CMAKE_CURRENT_LIST_DIR}/ft8/constants.c
               ${CMAKE_CURRENT_LIST_DIR}/ft8/crc.c
               ${CMAKE_CURRENT_LIST_DIR}/util/flashmem.c
               ${CMAKE_CURRENT_LIST_DIR}/wifi_setup.c
               ${CMAKE_CURRENT_LIST_DIR}/dhcpserver/dhcpserver.c
               ${CMAKE_CURRENT_LIST_DIR}/dnsserver/dnsserver.c
              )

pico_set_program_name(pico-wspr-tx "pico-wspr-tx")
pico_set_program_version(pico-wspr-tx "0.5")

pico_enable_stdio_uart(pico-wspr-tx 1)
pico_enable_stdio_usb(pico-wspr-tx 1)

target_compile_definitions(pico-wspr-tx PRIVATE
        PICO_INCLUDE_RTC_DATETIME=1
        LWIP_HTTPD_CGI=1
        LWIP_HTTPD_SSI=1
        LWIP_HTTPD_SSI_MULTIPART=1
        )


# Add the standard include files to the build
target_include_directories(pico-wspr-tx PRIVATE
                           ${CMAKE_CURRENT_LIST_DIR}
                           ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/piodco
                           ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/debug
                           ${CMAKE_CURRENT_LIST_DIR}/TxChannel
                           ${CMAKE_CURRENT_LIST_DIR}/WSPRbeacon
                           ${CMAKE_CURRENT_LIST_DIR}/WSPRbeacon/thirdparty
                           ${CMAKE_CURRENT_LIST_DIR}/util
                           ${CMAKE_CURRENT_LIST_DIR}/dhcpserver
                           ${CMAKE_CURRENT_LIST_DIR}/dnsserver
                           ${CMAKE_CURRENT_LIST_DIR}/..
                           ${PICO_SDK_PATH}/src/rp2_common/hardware_rtc/include
                          )

target_link_libraries(pico-wspr-tx
                      pico_stdlib
                      pico_sync
                      pico_multicore
                      hardware_timer
                      hardware_clocks
                      hardware_pio
                      hardware_powman  # RP2350 Power Manager for accurate timing
                      # hardware_rtc  # Disabled for Pico 2 W compatibility
                      # hardware_adc  # Removed ADC functionality
                      pico_lwip_http
                      pico_cyw43_arch_lwip_threadsafe_background
                     )

pico_add_extra_outputs(pico-wspr-tx)

# Add HTTP content library for WiFi setup web interface
pico_add_library(pico_wifi_setup_content NOFLAG)
pico_set_lwip_httpd_content(pico_wifi_setup_content INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/wifi_setup_content/404.html
    ${CMAKE_CURRENT_LIST_DIR}/wifi_setup_content/index.shtml
    ${CMAKE_CURRENT_LIST_DIR}/wifi_setup_content/wifi_config.shtml
)

target_link_libraries(pico-wspr-tx
    pico_wifi_setup_content
)
