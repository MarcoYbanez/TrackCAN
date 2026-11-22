cmake_minimum_required(VERSION 3.13)

# Host tests for the Teensy USB reboot request recognizer:
#   cmake -S DataLogger_Node/tests/bootloader_request -B build/bootloader_request_tests && cmake --build build/bootloader_request_tests && ./build/bootloader_request_tests/test_bootloader_request
project(bootloader_request_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(bootloader_request STATIC ${CMAKE_CURRENT_LIST_DIR}/../../src/bootloader/bootloader_request.c)
target_include_directories(bootloader_request PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../src/bootloader)
target_compile_options(bootloader_request PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)

enable_testing()
add_executable(test_bootloader_request test_bootloader_request.c)
target_link_libraries(test_bootloader_request PRIVATE bootloader_request)
target_compile_options(test_bootloader_request PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
target_link_options(test_bootloader_request PRIVATE -fsanitize=address,undefined)
add_test(NAME bootloader_request COMMAND test_bootloader_request)
