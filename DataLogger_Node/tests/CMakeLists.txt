cmake_minimum_required(VERSION 3.13)

# Host tests for the DataLogger's SD logger logic:
#   cmake -S DataLogger_Node/tests -B build/datalogger_tests && cmake --build build/datalogger_tests && ctest --test-dir build/datalogger_tests --output-on-failure
project(datalogger_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(logger_core STATIC ${CMAKE_CURRENT_LIST_DIR}/../src/logger/logger_core.c)
target_include_directories(logger_core PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../src/logger)
target_compile_options(logger_core PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)

enable_testing()
add_executable(test_logger_core test_logger_core.c)
target_link_libraries(test_logger_core PRIVATE logger_core)
target_compile_options(test_logger_core PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
target_link_options(test_logger_core PRIVATE -fsanitize=address,undefined)
add_test(NAME logger_core COMMAND test_logger_core)

# The USB log copy protocol (TCAN-21).
add_library(logger_xfer STATIC ${CMAKE_CURRENT_LIST_DIR}/../src/transfer/logger_xfer.c)
target_include_directories(logger_xfer PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../src/transfer)
target_compile_options(logger_xfer PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
add_executable(test_logger_xfer transfer/test_logger_xfer.c)
target_link_libraries(test_logger_xfer PRIVATE logger_xfer)
target_compile_options(test_logger_xfer PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
target_link_options(test_logger_xfer PRIVATE -fsanitize=address,undefined)
add_test(NAME logger_xfer COMMAND test_logger_xfer)

# The daily and manual logging rules (TCAN-20).
add_library(logger_segments STATIC ${CMAKE_CURRENT_LIST_DIR}/../src/logger/logger_segments.c)
target_include_directories(logger_segments PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../src/logger)
target_compile_options(logger_segments PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
add_executable(test_logger_segments test_logger_segments.c)
target_link_libraries(test_logger_segments PRIVATE logger_segments)
target_compile_options(test_logger_segments PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
target_link_options(test_logger_segments PRIVATE -fsanitize=address,undefined)
add_test(NAME logger_segments COMMAND test_logger_segments)
