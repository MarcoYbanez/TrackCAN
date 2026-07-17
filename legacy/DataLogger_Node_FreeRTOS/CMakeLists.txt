cmake_minimum_required(VERSION 3.13)

find_program(ARM_CC arm-none-eabi-gcc REQUIRED)
find_program(ARM_CXX arm-none-eabi-g++ REQUIRED)
find_program(ARM_ASM arm-none-eabi-gcc REQUIRED)
find_program(ARM_OBJCOP arm-none-eabi-objcopy REQUIRED)
find_program(ARM_SIZE arm-none-eabi-size REQUIRED)


set(CMAKE_C_COMPILER ${ARM_CC})
set(CMAKE_CXX_COMPILER ${ARM_CXX})
set(CMAKE_ASM_COMPILER ${ARM_ASM})
set(CMAKE_OBJCOPY ${ARM_OBJCOP})
set(CMAKE_SIZE ${ARM_SIZE})
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(DATALOGGER_PATH ${CMAKE_CURRENT_SOURCE_DIR})



# PROJECT
project(data_logger_node C CXX ASM)

set(MCU IMXRT1062)
set(MCU_LD imxrt1062_t41.ld)
set(MCU_DEF ARDUINO_TEENSY41)
set(TEENSY_COMPILE_FLAGS
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)

# FREERTOS
set(FREERTOS_PORT GCC_ARM_CM7)
include(FreeRTOS_Kernel_import.cmake)


# TEENSY CORE
add_subdirectory(
		${TRACKCAN_PATH}/external/teensy41
		${TRACKCAN_PATH}/external/teensy41/build
)

file(GLOB DATA_LOGGER_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/aux_lights/*.c
		${CMAKE_CURRENT_SOURCE_DIR}/aux_lights/*.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/data_logger/*.c
		${CMAKE_CURRENT_SOURCE_DIR}/data_logger/*.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/engine_sensors/*.c
		${CMAKE_CURRENT_SOURCE_DIR}/engine_sensors/*.cpp
)

add_executable(data_logger_node 
		main.cpp
		${DATA_LOGGER_SOURCES}
)


target_include_directories(data_logger_node PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/aux_lights
		${CMAKE_CURRENT_SOURCE_DIR}/data_logger
		${CMAKE_CURRENT_SOURCE_DIR}/engine_sensors
)

set_target_properties(data_logger_node PROPERTIES
		SUFFIX ".elf"
)

add_custom_command(TARGET data_logger_node POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:data_logger_node>
    COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom
            $<TARGET_FILE:data_logger_node>
            $<TARGET_FILE_DIR:data_logger_node>/$<TARGET_FILE_BASE_NAME:data_logger_node>.hex
    COMMENT "Generating HEX file"
		COMMAND ${CMAKE_COMMAND} -E make_directory ${TRACKCAN_OUTPUT_DIR}
		COMMAND ${CMAKE_COMMAND} -E copy 
				${CMAKE_CURRENT_BINARY_DIR}/data_logger_node.hex
				${TRACKCAN_OUTPUT_DIR}
		COMMENT "Copying data_logger_node.hex to TrackCAN firmware output directory"
)

target_link_libraries(data_logger_node PRIVATE 
		teensy41_core
	    freertos_kernel
)




