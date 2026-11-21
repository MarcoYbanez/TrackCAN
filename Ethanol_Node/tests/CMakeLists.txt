cmake_minimum_required(VERSION 3.13)

# Host build of the Ethanol Node safety logic and its tests:
#   cmake -S Ethanol_Node/tests -B build/ethanol_tests && cmake --build build/ethanol_tests \
#     && ctest --test-dir build/ethanol_tests --output-on-failure
#
# Built with the address and undefined-behavior sanitizers: the original code had an undefined
# float-to-int cast, so any such bug must fail loudly here.
project(ethanol_safety_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SAFETY_DIR ${CMAKE_CURRENT_LIST_DIR}/../safety)

add_library(ethanol_safety STATIC
    ${SAFETY_DIR}/ethanol_signal.c
    ${SAFETY_DIR}/ethanol_safety.c
    ${SAFETY_DIR}/ethanol_dac_math.c
    ${SAFETY_DIR}/ethanol_health.c
    ${SAFETY_DIR}/ethanol_scratch.c
    ${SAFETY_DIR}/ethanol_boot.c
)
target_include_directories(ethanol_safety PUBLIC ${SAFETY_DIR})
target_compile_options(ethanol_safety PRIVATE -Wall -Wextra -Wconversion -fsanitize=address,undefined -fno-sanitize-recover=undefined)
target_link_options(ethanol_safety PUBLIC -fsanitize=address,undefined)

enable_testing()
add_executable(test_ethanol_safety test_ethanol_safety.c)
target_link_libraries(test_ethanol_safety PRIVATE ethanol_safety)
target_compile_options(test_ethanol_safety PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
add_test(NAME ethanol_safety COMMAND test_ethanol_safety)
