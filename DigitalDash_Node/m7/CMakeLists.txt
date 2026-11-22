cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dash_m7)

# TRACKCAN_PATH is the repository root.
if(NOT DEFINED TRACKCAN_PATH)
    set(TRACKCAN_PATH ${CMAKE_CURRENT_LIST_DIR}/../..)
endif()

target_sources(app PRIVATE
    src/main.c
    ../shared/dash_shared.c
    ../shared/dash_view.c
    ../shared/dash_geometry.c
    ../shared/dash_render.c
    ../shared/dash_health.c
    ../common/dash_watchdog.c
)

# The SquareLine export (images, fonts, screen). Re-export over it and rebuild: nothing here is hand-edited.
set(UI_DIR ${TRACKCAN_PATH}/UI/DigitalDash_480x272/lvgl9_export)
file(GLOB UI_SRC ${UI_DIR}/*.c ${UI_DIR}/screens/*.c ${UI_DIR}/components/*.c
     ${UI_DIR}/images/*.c ${UI_DIR}/fonts/*.c)
target_sources(app PRIVATE ${UI_SRC})
target_include_directories(app PRIVATE ${UI_DIR} ${CMAKE_CURRENT_LIST_DIR}/shim)
target_include_directories(app PRIVATE
    ../shared
    ../common
    ${TRACKCAN_PATH}/can
    ${TRACKCAN_PATH}/can/common
    ${TRACKCAN_PATH}/can/generated
)

# Test hook: hang this core on purpose after N ms so the watchdog can be measured on the bench. Never set in a car build.
if(DEFINED DASH_TEST_HANG_AFTER_MS)
    target_compile_definitions(app PRIVATE DASH_TEST_HANG_AFTER_MS=${DASH_TEST_HANG_AFTER_MS})
    message(WARNING "Dash M7: TEST BUILD, hangs after ${DASH_TEST_HANG_AFTER_MS} ms")
endif()
