cmake_minimum_required(VERSION 3.20)
project(riscv_t527_a733_apps C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_CPPUTEST_TESTS "Enable CppUTest-based host simulation tests" ON)
option(BUILD_RISCV_FIRMWARE "Build bare-metal RISC-V firmware targets" ON)
# CSR encodings for mhcr/mcor are unverified against the E906 manual; opt in only
# after confirming on silicon.
option(ENABLE_XUANTIE_ICACHE "Enable the XuanTie instruction cache at startup" OFF)

add_subdirectory(third_party/pigweed)

# Include Directories for the firmware side when enabled.
if(BUILD_RISCV_FIRMWARE)
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/hal/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/AbstractX/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/etl/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/barectf
    )

    # Keep coroutine frames out of the 64 KB DTCM that also carries the stack.
    add_compile_definitions(ABSTRACTX_CORO_POOL_SECTION=".coro_frame_pool")

    if(ENABLE_XUANTIE_ICACHE)
        add_compile_definitions(ENABLE_XUANTIE_ICACHE)
    endif()
endif()

if(BUILD_CPPUTEST_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_RISCV_FIRMWARE)
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/hal/src/riscv_memory_map.ld")
    set(COMMON_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/hal/src/startup.S
        ${CMAKE_CURRENT_SOURCE_DIR}/hal/src/resource_table.c
        ${CMAKE_CURRENT_SOURCE_DIR}/hal/src/riscv_trap_handler.cpp
    )

    # 1. Hello World App
    add_executable(hello_world.elf
        ${COMMON_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/hello_world/src/main.cpp
    )
    target_link_libraries(hello_world.elf PRIVATE Pigweed::pigweed)
    target_link_options(hello_world.elf PRIVATE -T${LINKER_SCRIPT} -Wl,-Map=hello_world.map)

    # 2. IPC Benchmark App
    add_executable(ipc_benchmark.elf
        ${COMMON_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/ipc_benchmark/src/main.cpp
    )
    target_link_libraries(ipc_benchmark.elf PRIVATE Pigweed::pigweed)
    target_link_options(ipc_benchmark.elf PRIVATE -T${LINKER_SCRIPT} -Wl,-Map=ipc_benchmark.map)

    # 3. RISC-V timer/CLINT bring-up test.
    add_executable(timer_test.elf
        ${CMAKE_CURRENT_SOURCE_DIR}/hal/src/startup.S
        ${CMAKE_CURRENT_SOURCE_DIR}/hal/src/resource_table.c
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/timer_test/src/main.c
    )
    target_link_options(timer_test.elf PRIVATE -T${LINKER_SCRIPT} -Wl,-Map=timer_test.map)

    # 4. Pin routing smoke test: walks every RISC-V-owned pin with an
    #    identifying pulse burst so the 40-pin header can be probed.
    add_executable(platform_smoke_test.elf
        ${COMMON_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/platform_smoke_test/src/main.cpp
    )
    target_link_options(platform_smoke_test.elf PRIVATE
        -T${LINKER_SCRIPT} -Wl,-Map=platform_smoke_test.map)

    # 5. Controlled Fault / Trap Test
    add_executable(fault_test.elf
        ${COMMON_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/fault_test/src/main.cpp
    )
    target_link_options(fault_test.elf PRIVATE
        -T${LINKER_SCRIPT} -Wl,-Map=fault_test.map)

    # 6. Bidirectional IPC Ping-Pong Test
    add_executable(ipc_ping_test.elf
        ${COMMON_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/ipc_ping_test/src/main.cpp
    )
    target_link_options(ipc_ping_test.elf PRIVATE
        -T${LINKER_SCRIPT} -Wl,-Map=ipc_ping_test.map)
endif()

# Linux-side IMU IPC demo: a host application, never cross-compiled for the E906.
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(linux_imu_demo
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/linux_imu_demo/src/main.cpp
    )
    target_link_libraries(linux_imu_demo PRIVATE Pigweed::pigweed)
endif()
