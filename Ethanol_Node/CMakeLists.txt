cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "/Users/openinvite/Documents/DashProject/pico-sdk" CACHE PATH "Pico SDK path")
set(FREERTOS_KERNEL_PATH "/Users/openinvite/Documents/DashProject/FreeRTOS-Kernel" CACHE PATH "FreeRTOS kernel path")
set(PICO_BOARD adafruit_feather_rp2040 CACHE STRING "Pico board")

include(pico_sdk_import.cmake)
include(FreeRTOS_Kernel_import.cmake)

project(ethanol_node C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(ethanol_node
    main.c
)

target_include_directories(ethanol_node PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(ethanol_node
    pico_stdlib
		hardware_i2c
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

pico_add_extra_outputs(ethanol_node)
pico_enable_stdio_usb(ethanol_node 1)
pico_enable_stdio_uart(ethanol_node 0)
