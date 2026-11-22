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
    ../shared/dash_sim.c
    ../shared/dash_health.c
    ../common/dash_watchdog.c
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

# Build mode. ON (the default until the FDCAN port exists, TCAN-6) publishes the DEMO drive and sets the DEMO flag,
# which makes the M7 draw a DEMO banner and frame on every frame. OFF is the live image: it publishes only what the CAN
# logic received, and shows "--" for anything it has no source for. Use: -DDASH_DEMO=OFF
option(DASH_DEMO "Publish the demo drive (bench only); OFF builds the live image" ON)
if(DASH_DEMO)
    target_compile_definitions(app PRIVATE DASH_DEMO=1)
    message(STATUS "Dash M4: DEMO image (fake data, DEMO banner on screen)")
else()
    message(STATUS "Dash M4: LIVE image")
endif()

# Test hook: hang this core on purpose after N ms so the watchdog can be measured on the bench. Never set in a car build.
if(DEFINED DASH_TEST_HANG_AFTER_MS)
    target_compile_definitions(app PRIVATE DASH_TEST_HANG_AFTER_MS=${DASH_TEST_HANG_AFTER_MS})
    message(WARNING "Dash M4: TEST BUILD, hangs after ${DASH_TEST_HANG_AFTER_MS} ms")
endif()
