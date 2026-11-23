cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(solo_blink)

# Shared TrackCAN library (also used by the other Nodes). The root CMakeLists passes
# TRACKCAN_PATH; fall back to the parent directory for standalone builds.
if(NOT DEFINED TRACKCAN_PATH)
	set(TRACKCAN_PATH ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

# The SD card logger. -DDATALOGGER_SD=OFF builds the firmware without it (and the Kconfig for it is switched off by the
# script), to tell a problem in the logger from a problem in the rest of the firmware.
option(DATALOGGER_SD "Build the SD card logger" ON)

# The TrackCAN node logic. -DDATALOGGER_CAN=OFF builds the original bring-up scaffold without it (with CONFIG_CAN=n), a control for
# telling a problem in the CAN driver from a problem elsewhere.
option(DATALOGGER_CAN "Build the TrackCAN CAN logic" ON)
# Bench only: the CAN controller runs in internal loopback, so a lone Node produces steady traffic. The log says so (a NOTE line).
option(DATALOGGER_CAN_LOOPBACK "CAN controller in loopback (bench)" OFF)

target_sources(app PRIVATE
	src/main.c
	src/bootloader/bootloader_control.c
	src/bootloader/bootloader_request.c
)
if(DATALOGGER_CAN)
	target_sources(app PRIVATE
		${TRACKCAN_PATH}/can/datalogger.c
		${TRACKCAN_PATH}/can/common/trackcan_core.c
		${TRACKCAN_PATH}/can/generated/trackcan.c
	)
else()
	message(STATUS "DataLogger: built WITHOUT the CAN logic")
endif()
target_include_directories(app PRIVATE
	src/logger
	src/bootloader
	${TRACKCAN_PATH}/can
	${TRACKCAN_PATH}/can/common
	${TRACKCAN_PATH}/can/generated
)

if(DATALOGGER_CAN_LOOPBACK)
	target_compile_definitions(app PRIVATE DATALOGGER_CAN_LOOPBACK=1)
endif()
# Bench only: a rising-rate frame generator into the SD logger queue, to find where frames start to drop.
option(DATALOGGER_SD_BENCH_CUTTEST "SD logger: console timestamp every 100 ms (bench, measures what a power cut loses)" OFF)
if(DATALOGGER_SD_BENCH_CUTTEST)
	target_compile_definitions(app PRIVATE DATALOGGER_SD_BENCH_CUTTEST=1)
endif()
option(DATALOGGER_SD_BENCH_FLOOD "SD logger flood generator (bench)" OFF)
if(DATALOGGER_SD_BENCH_FLOOD)
	target_compile_definitions(app PRIVATE DATALOGGER_SD_BENCH_FLOOD=1)
endif()

# New log file every N seconds or N KB, whichever comes first (0 = no limit). The defaults are the real values; the bench builds shorten them.
set(DATALOGGER_SD_ROTATE_SECONDS 600 CACHE STRING "Seconds before a new log file (0 = no limit)")
set(DATALOGGER_SD_ROTATE_KB 30720 CACHE STRING "KB before a new log file (0 = no limit)")
target_compile_definitions(app PRIVATE
	DATALOGGER_SD_ROTATE_SECONDS=${DATALOGGER_SD_ROTATE_SECONDS}
	DATALOGGER_SD_ROTATE_KB=${DATALOGGER_SD_ROTATE_KB})

if(DATALOGGER_SD)
	target_sources(app PRIVATE
		src/logger/logger_core.c
		src/logger/sd_logger.c
	)
else()
	target_compile_definitions(app PRIVATE DATALOGGER_NO_SD=1)
	message(STATUS "DataLogger: built WITHOUT the SD logger")
endif()
