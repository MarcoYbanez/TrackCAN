cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dash_m4)

# The shared TrackCAN library. The M4 runs the CAN logic (M2); for now it only publishes test
# values. TRACKCAN_PATH is the repository root.
if(NOT DEFINED TRACKCAN_PATH)
    set(TRACKCAN_PATH ${CMAKE_CURRENT_LIST_DIR}/../..)
endif()

target_sources(app PRIVATE
    src/main.c
    ../shared/dash_shared.c
    ${TRACKCAN_PATH}/can/dash.c
    ${TRACKCAN_PATH}/can/dash_port_stm32h7.c
    ${TRACKCAN_PATH}/can/common/trackcan_core.c
    ${TRACKCAN_PATH}/can/generated/trackcan.c
)
target_include_directories(app PRIVATE
    ../shared
    ../common
    ${TRACKCAN_PATH}/can
    ${TRACKCAN_PATH}/can/common
    ${TRACKCAN_PATH}/can/generated
)
