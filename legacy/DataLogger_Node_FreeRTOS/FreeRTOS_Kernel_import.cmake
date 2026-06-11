# This is a copy of <FREERTOS_KERNEL_PATH>/portable/ThirdParty/GCC/RP2040/FREERTOS_KERNEL_import.cmake

# This can be dropped into an external project to help locate the FreeRTOS kernel
# It should be include()ed prior to project(). Alternatively this file may
# or the CMakeLists.txt in this directory may be included or added via add_subdirectory
# respectively.

if (DEFINED ENV{FREERTOS_KERNEL_PATH} AND (NOT FREERTOS_KERNEL_PATH))
    set(FREERTOS_KERNEL_PATH $ENV{FREERTOS_KERNEL_PATH})
    message("Using FREERTOS_KERNEL_PATH from environment ('${FREERTOS_KERNEL_PATH}')")
endif ()

# if (NOT FREERTOS_KERNEL_PATH)
#     # check if we are inside the FreeRTOS kernel tree (i.e. this file has been included directly)
#     get_filename_component(_ACTUAL_PATH ${CMAKE_CURRENT_LIST_DIR} REALPATH)
#     get_filename_component(_POSSIBLE_PATH ${CMAKE_CURRENT_LIST_DIR}/${FREERTOS_KERNEL_RP2040_BACK_PATH}/${FREERTOS_KERNEL_RP2040_RELATIVE_PATH} REALPATH)
#     if (_ACTUAL_PATH STREQUAL _POSSIBLE_PATH)
#         get_filename_component(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/${FREERTOS_KERNEL_RP2040_BACK_PATH} REALPATH)
#     endif()
#     if (_ACTUAL_PATH STREQUAL _POSSIBLE_PATH)
#         get_filename_component(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/${FREERTOS_KERNEL_RP2040_BACK_PATH} REALPATH)
#         message("Setting FREERTOS_KERNEL_PATH to ${FREERTOS_KERNEL_PATH} based on location of FreeRTOS-Kernel-import.cmake")
#     elseif (PICO_SDK_PATH AND EXISTS "${PICO_SDK_PATH}/../FreeRTOS-Kernel")
#         set(FREERTOS_KERNEL_PATH ${PICO_SDK_PATH}/../FreeRTOS-Kernel)
#         message("Defaulting FREERTOS_KERNEL_PATH as sibling of PICO_SDK_PATH: ${FREERTOS_KERNEL_PATH}")
#     endif()
# endif ()
#
# if (NOT FREERTOS_KERNEL_PATH)
#     foreach(POSSIBLE_SUFFIX Source FreeRTOS-Kernel FreeRTOS/Source)
#         # check if FreeRTOS-Kernel exists under directory that included us
#         set(SEARCH_ROOT ${CMAKE_CURRENT_SOURCE_DIR}})
#         set(SEARCH_ROOT ../../../..)
#         get_filename_component(_POSSIBLE_PATH ${SEARCH_ROOT}/${POSSIBLE_SUFFIX} REALPATH)
#         if (EXISTS ${_POSSIBLE_PATH}/${FREERTOS_KERNEL_RP2040_RELATIVE_PATH}/CMakeLists.txt)
#             get_filename_component(FREERTOS_KERNEL_PATH ${_POSSIBLE_PATH} REALPATH)
#             message("Setting FREERTOS_KERNEL_PATH to '${FREERTOS_KERNEL_PATH}' found relative to enclosing project")
#             break()
#         endif()
#     endforeach()
# endif()
#
# if (NOT FREERTOS_KERNEL_PATH)
#     message(FATAL_ERROR "FreeRTOS location was not specified. Please set FREERTOS_KERNEL_PATH.")
# endif()
#
# set(FREERTOS_KERNEL_PATH "${FREERTOS_KERNEL_PATH}" CACHE PATH "Path to the FreeRTOS Kernel")
#


set(FREERTOS_KERNEL_PATH ${TRACKCAN_PATH}/external/FreeRTOS-Kernel)
add_library(freertos_kernel STATIC 
		${FREERTOS_KERNEL_PATH}/tasks.c
    ${FREERTOS_KERNEL_PATH}/queue.c
    ${FREERTOS_KERNEL_PATH}/list.c
    ${FREERTOS_KERNEL_PATH}/timers.c
    ${FREERTOS_KERNEL_PATH}/event_groups.c
    ${FREERTOS_KERNEL_PATH}/stream_buffer.c
    ${FREERTOS_KERNEL_PATH}/portable/MemMang/heap_4.c

    ${FREERTOS_KERNEL_PATH}/portable/GCC/ARM_CM7/r0p1/port.c
)
target_include_directories(freertos_kernel PUBLIC 
    ${FREERTOS_KERNEL_PATH}/include
    ${FREERTOS_KERNEL_PATH}/portable/GCC/ARM_CM7/r0p1
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(freertos_kernel PRIVATE
    ${TEENSY_COMPILE_FLAGS}
)

target_compile_definitions(freertos_kernel PUBLIC
    projCOVERAGE_TEST=0
)

target_compile_options(freertos_kernel PRIVATE
		${TEENSY_COMPILE_FLAGS}
)




add_library(freertos_config INTERFACE)

target_include_directories(freertos_config INTERFACE 
		${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(freertos_config INTERFACE
		projCOVERAGE_TEST=0
)

# add_subdirectory(
# 		${TRACKCAN_PATH}/external/FreeRTOS-Kernel
# 		${CMAKE_CURRENT_BINARY_DIR}/FreeRTOS_datalogger
# )
# include(FreeRTOS_Kernel_import.cmake)






#
# target_compile_options(freertos_kernel_port PRIVATE
# 		${TEENSY_COMPILE_FLAGS}
# )


