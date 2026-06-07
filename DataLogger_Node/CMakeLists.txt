cmake_minimum_required(VERSION 3.13)

# include(FreeRTOS_Kernel_import.cmake)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(MCU IMXRT1062)
set(MCU_LD imxrt1062_t41.ld)
set(MCU_DEF ARDUINO_TEENSY41)


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


project(data_logger_node C CXX ASM)

add_subdirectory(
		${TRACKCAN_PATH}/external/teensy41
		${CMAKE_CURRENT_BINARY_DIR}/teensy41_core_build
)


add_executable(data_logger_node 
		main.cpp
)

set_target_properties(data_logger_node PROPERTIES
		SUFFIC ".elf"
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

target_link_libraries(data_logger_node PRIVATE teensy41_core)



