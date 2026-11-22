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
)
target_include_directories(app PRIVATE
    ../shared
    ../common
    ${TRACKCAN_PATH}/can
    ${TRACKCAN_PATH}/can/common
    ${TRACKCAN_PATH}/can/generated
)
