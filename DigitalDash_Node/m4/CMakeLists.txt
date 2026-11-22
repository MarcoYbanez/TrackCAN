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
    ../shared/dash_loopback_source.c
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

# Build mode (-DDASH_MODE=live|loopback|demo; the script passes it):
#   live     the default. Real FDCAN on the bus; publishes only what the CAN logic received. Anything without a source (speed,
#            RPM, boost, battery, AFR, gear: TCAN-7 and TCAN-4) stays invalid and shows "--".
#   loopback bench only, no wiring. The FDCAN controller runs in internal loopback and simulated DataLogger and Ethanol Nodes
#            (dash_loopback_source) send real TrackCAN frames through it, so the whole receive path to the screen is exercised.
#            The data is simulated, so the DEMO flag is set.
#   demo     bench only. Fake values straight into the snapshot, no CAN at all. DEMO flag set.
set(DASH_MODE "live" CACHE STRING "live, loopback or demo")
if(NOT DASH_MODE MATCHES "^(live|loopback|demo)$")
    message(FATAL_ERROR "DASH_MODE must be live, loopback or demo (got '${DASH_MODE}')")
endif()
string(TOUPPER ${DASH_MODE} DASH_MODE_UPPER)
target_compile_definitions(app PRIVATE DASH_MODE_${DASH_MODE_UPPER}=1)
if(DASH_MODE STREQUAL "live")
    message(STATUS "Dash M4: LIVE image (real CAN data only)")
else()
    message(STATUS "Dash M4: ${DASH_MODE_UPPER} image (simulated data, DEMO banner on screen)")
endif()

# Test hook: hang this core on purpose after N ms so the watchdog can be measured on the bench. Never set in a car build.
if(DEFINED DASH_TEST_HANG_AFTER_MS)
    target_compile_definitions(app PRIVATE DASH_TEST_HANG_AFTER_MS=${DASH_TEST_HANG_AFTER_MS})
    message(WARNING "Dash M4: TEST BUILD, hangs after ${DASH_TEST_HANG_AFTER_MS} ms")
endif()
