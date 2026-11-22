cmake_minimum_required(VERSION 3.13)

# Host build of the Dash's cross-core exchange and its tests:
#   cmake -S DigitalDash_Node/tests -B build/dash_tests && cmake --build build/dash_tests \
#     && ctest --test-dir build/dash_tests --output-on-failure
project(dash_shared_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(TRACKCAN_PATH ${CMAKE_CURRENT_LIST_DIR}/../..)
set(DASH_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR}/../shared)

add_library(dash_shared STATIC
    ${DASH_SHARED_DIR}/dash_shared.c
    ${DASH_SHARED_DIR}/dash_view.c
    ${DASH_SHARED_DIR}/dash_sim.c
    ${DASH_SHARED_DIR}/dash_geometry.c
    ${DASH_SHARED_DIR}/dash_render.c
    ${TRACKCAN_PATH}/can/dash.c
    ${TRACKCAN_PATH}/can/dash_port_stm32h7.c
    ${TRACKCAN_PATH}/can/common/trackcan_core.c
    ${TRACKCAN_PATH}/can/generated/trackcan.c
)
target_include_directories(dash_shared PUBLIC
    ${DASH_SHARED_DIR}
    ${TRACKCAN_PATH}/can
    ${TRACKCAN_PATH}/can/common
    ${TRACKCAN_PATH}/can/generated
)
target_compile_definitions(dash_shared PRIVATE DASH_READ_MIDPOINT_HOOK=dash_test_midpoint)
target_compile_options(dash_shared PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
target_link_options(dash_shared PUBLIC -fsanitize=address,undefined)

enable_testing()
add_executable(test_dash_shared test_dash_shared.c)
target_link_libraries(test_dash_shared PRIVATE dash_shared)
target_compile_options(test_dash_shared PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
add_test(NAME dash_shared COMMAND test_dash_shared)

add_executable(test_dash_view test_dash_view.c)
target_link_libraries(test_dash_view PRIVATE dash_shared)
target_compile_options(test_dash_view PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
add_test(NAME dash_view COMMAND test_dash_view)

add_executable(test_dash_render test_dash_render.c)
target_link_libraries(test_dash_render PRIVATE dash_shared)
target_include_directories(test_dash_render PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(test_dash_render PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
add_test(NAME dash_render COMMAND test_dash_render)

add_executable(test_dash_sim test_dash_sim.c)
target_link_libraries(test_dash_sim PRIVATE dash_shared)
target_compile_options(test_dash_sim PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
add_test(NAME dash_sim COMMAND test_dash_sim)
