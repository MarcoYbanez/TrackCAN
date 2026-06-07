
cmake_minimum_required(VERSION 3.13)

project(teensy41_core C CXX ASM)

set(TEENSY_CORE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/core)

message("${TEENSY_CORE_PATH}")

file(GLOB_RECURSE TEENSY41_CORE_GLOB CONFIGURE_DEPENDS
		${TEENSY_CORE_PATH}/*.c
		${TEENSY_CORE_PATH}/*.cpp
		${TEENSY_CORE_PATH}/*.s
		${TEENSY_CORE_PATH}/*.S
)

add_library(teensy41_core STATIC
		${TEENSY41_CORE_GLOB}
)

target_include_directories(teensy41_core PUBLIC
		${TEENSY_CORE_PATH}
)

target_compile_definitions(teensy41_core PUBLIC
		F_CPU=600000000 
		USB_SERIAL 
		LAYOUT_US_ENGLISH 
		USING_MAKEFILE
		__${MCU}__ 
		ARDUINO=10813 
		TEENSYDUINO=159 
		${MCU_DEF}
)

target_compile_options(teensy41_core PUBLIC
		-mcpu=cortex-m7 
		-mfloat-abi=hard 
		-mfpu=fpv5-d16 
		-mthumb
)

target_compile_options(teensy41_core PUBLIC
    $<$<COMPILE_LANGUAGE:CXX>:
        -felide-constructors
        -fno-exceptions
        -fpermissive
        -fno-rtti
        -Wno-error=narrowing
				>
		)

target_compile_options(teensy41_core PUBLIC
    -Wall
    -g
    -O2
    -ffunction-sections
    -fdata-sections
)

target_link_options(teensy41_core INTERFACE
    -Os
    -Wl,--gc-sections,--relax
    -mcpu=cortex-m7
    -mfloat-abi=hard
    -mfpu=fpv5-d16
    -mthumb
		-T${TEENSY_CORE_PATH}/imxrt1062_t41.ld
)

# target_link_directories(teensy4_core INTERFACE ${CMAKE_CURRET_SOURCE_DIR}/arm_lib)

target_link_libraries(teensy41_core INTERFACE
		${CMAKE_CURRENT_SOURCE_DIR}/lib/libarm_cortexM7lfsp_math.a
    m
    stdc++
)

