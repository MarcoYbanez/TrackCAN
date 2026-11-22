cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(solo_blink)

# Shared TrackCAN library (also used by the other Nodes). The root CMakeLists passes
# TRACKCAN_PATH; fall back to the parent directory for standalone builds.
if(NOT DEFINED TRACKCAN_PATH)
	set(TRACKCAN_PATH ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

target_sources(app PRIVATE
	src/main.c
	src/bootloader/bootloader_control.c
	src/bootloader/bootloader_request.c
	${TRACKCAN_PATH}/can/datalogger.c
	${TRACKCAN_PATH}/can/common/trackcan_core.c
	${TRACKCAN_PATH}/can/generated/trackcan.c
)
target_include_directories(app PRIVATE
	src/bootloader
	${TRACKCAN_PATH}/can
	${TRACKCAN_PATH}/can/common
	${TRACKCAN_PATH}/can/generated
)
