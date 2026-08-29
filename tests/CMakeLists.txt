cmake_minimum_required(VERSION 3.20)

include(FetchContent)
include(CTest)

# Host-side CppUTest tests
find_package(CppUTest QUIET)
if(NOT CppUTest_FOUND)
    message(STATUS "CppUTest not found; fetching via FetchContent")
    FetchContent_Declare(
        cpputest
        GIT_REPOSITORY https://github.com/cpputest/cpputest.git
        GIT_TAG v4.0
    )
    FetchContent_MakeAvailable(cpputest)
endif()

# Linux host simulation tests
add_executable(io_processor_host_tests
    test_main.cpp
    host/test_boot_status.cpp
    host/test_clock_config.cpp
    host/test_domain_queue.cpp
    host/test_io_processor_ring.cpp
    host/test_pigweed_compat.cpp
)

target_link_libraries(io_processor_host_tests PRIVATE
    CppUTest::CppUTestExt
    CppUTest::CppUTest
    Pigweed::pigweed
)

target_include_directories(io_processor_host_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../hal/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/AbstractX/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/etl/include
)

# RISC-V-targeted simulation tests: compiled for host but modeling the coprocessor
# behavior and memory layout. This is a simulation harness, not the final bare-metal binary.
add_executable(io_processor_riscv_sim_tests
    test_main.cpp
    riscv/test_riscv_memory_layout.cpp
)

target_link_libraries(io_processor_riscv_sim_tests PRIVATE
    CppUTest::CppUTestExt
    CppUTest::CppUTest
)

target_include_directories(io_processor_riscv_sim_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(NAME io_processor_host_tests COMMAND io_processor_host_tests)
add_test(NAME io_processor_riscv_sim_tests COMMAND io_processor_riscv_sim_tests)
