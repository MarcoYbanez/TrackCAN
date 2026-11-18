cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD adafruit_feather_rp2040 CACHE STRING "Pico board")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include(FreeRTOS_Kernel_import.cmake)

project(ethanol_node C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Shared TrackCAN library (also used by the other Nodes). The root CMakeLists passes
# TRACKCAN_PATH; fall back to the parent directory for standalone builds.
if(NOT DEFINED TRACKCAN_PATH)
    set(TRACKCAN_PATH ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

add_executable(ethanol_node
    main.c
    ${TRACKCAN_PATH}/can/ethanol.c
    ${TRACKCAN_PATH}/can/common/trackcan_core.c
    ${TRACKCAN_PATH}/can/generated/trackcan.c
)

target_include_directories(ethanol_node PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${TRACKCAN_PATH}/can
    ${TRACKCAN_PATH}/can/common
    ${TRACKCAN_PATH}/can/generated
)

target_link_libraries(ethanol_node
    pico_stdlib
		hardware_i2c
		hardware_spi
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

pico_add_extra_outputs(ethanol_node)
pico_enable_stdio_usb(ethanol_node 1)
pico_enable_stdio_uart(ethanol_node 0)

add_custom_command(TARGET ethanol_node POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory ${TRACKCAN_OUTPUT_DIR}
		COMMAND ${CMAKE_COMMAND} -E copy 
				${CMAKE_CURRENT_BINARY_DIR}/ethanol_node.uf2
				${TRACKCAN_OUTPUT_DIR}
		COMMENT "Copying ethanol_node.uf2 to TrackCAN firmware output directory"
)
