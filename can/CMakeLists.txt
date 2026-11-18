cmake_minimum_required(VERSION 3.13)

# Host build of the shared TrackCAN library and its tests:
#   cmake -S can -B build/can_tests && cmake --build build/can_tests && ctest --test-dir build/can_tests --output-on-failure
#
# The firmware builds (Zephyr, Pico SDK) compile the same sources directly, see TRACKCAN_CORE_SOURCES.
project(trackcan_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(TRACKCAN_CORE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/generated/trackcan.c
    ${CMAKE_CURRENT_LIST_DIR}/common/trackcan_core.c
)
# Dash logic is hardware-independent, so it is built and tested on the host too.
set(TRACKCAN_DASH_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/dash.c
    ${CMAKE_CURRENT_LIST_DIR}/dash_port_stm32h7.c
)
set(TRACKCAN_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/generated
    ${CMAKE_CURRENT_LIST_DIR}/common
    ${CMAKE_CURRENT_LIST_DIR}
)

add_library(trackcan STATIC ${TRACKCAN_CORE_SOURCES} ${TRACKCAN_DASH_SOURCES})
target_include_directories(trackcan PUBLIC ${TRACKCAN_INCLUDE_DIRS})
target_compile_options(trackcan PRIVATE -Wall -Wextra)

enable_testing()
add_executable(test_trackcan tests/test_trackcan.c)
target_link_libraries(test_trackcan PRIVATE trackcan)
target_compile_options(test_trackcan PRIVATE -Wall -Wextra)
add_test(NAME trackcan COMMAND test_trackcan)
