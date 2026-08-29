# xPack RISC-V GCC Toolchain Setup with Automatic Fetch Support
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

# This network check runs during CMake configuration only, never during a build.
option(CHECK_XPACK_UPDATES "Notify when a newer xPack RISC-V GCC release is available" ON)

# Prefer an explicitly selected compiler, then any xPack installed under ~/.tools.
file(GLOB XPACK_TOOLCHAIN_BIN_DIRS "$ENV{HOME}/.tools/xpack-riscv-none-elf-gcc-*/bin")
find_program(RISCV_GCC_EXECUTABLE
    NAMES riscv-none-elf-gcc
    PATHS
        ${XPACK_TOOLCHAIN_BIN_DIRS}
        "$ENV{HOME}/.local/bin"
        "/opt/xpack-riscv-none-elf-gcc/bin"
        "/usr/local/bin"
        "/usr/bin"
)

if(NOT RISCV_GCC_EXECUTABLE)
    message(STATUS "xPack RISC-V GCC not found; installing it under $ENV{HOME}/.tools.")
    include(${CMAKE_CURRENT_LIST_DIR}/fetch_xpack.cmake)
endif()

if(NOT EXISTS "${RISCV_GCC_EXECUTABLE}")
    message(FATAL_ERROR "RISC-V compiler was not found: ${RISCV_GCC_EXECUTABLE}")
endif()

get_filename_component(TOOLCHAIN_BIN_DIR "${RISCV_GCC_EXECUTABLE}" DIRECTORY)
set(TOOLCHAIN_PREFIX "${TOOLCHAIN_BIN_DIR}/riscv-none-elf-")

# Report, but never download or select, a newer release automatically.
if(CHECK_XPACK_UPDATES AND NOT DEFINED XPACK_UPDATE_CHECK_THIS_CONFIGURE)
    set(XPACK_UPDATE_CHECK_THIS_CONFIGURE TRUE)
    get_filename_component(XPACK_TOOLCHAIN_ROOT "${TOOLCHAIN_BIN_DIR}" DIRECTORY)
    string(REGEX MATCH "xpack-riscv-none-elf-gcc-([0-9]+\\.[0-9]+\\.[0-9]+-[0-9]+)$"
        _xpack_version_match "${XPACK_TOOLCHAIN_ROOT}")
    set(XPACK_INSTALLED_VERSION "${CMAKE_MATCH_1}")

    if(XPACK_INSTALLED_VERSION)
        set(XPACK_RELEASE_INFO "${CMAKE_BINARY_DIR}/CMakeFiles/xpack-riscv-release.json")
        file(DOWNLOAD
            "https://api.github.com/repos/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/latest"
            "${XPACK_RELEASE_INFO}"
            STATUS XPACK_RELEASE_STATUS
            TIMEOUT 5
            TLS_VERIFY ON
            HTTPHEADER "Accept: application/vnd.github+json" "User-Agent: riscv-t527-a733-cmake")
        list(GET XPACK_RELEASE_STATUS 0 XPACK_RELEASE_STATUS_CODE)
        if(XPACK_RELEASE_STATUS_CODE EQUAL 0 AND EXISTS "${XPACK_RELEASE_INFO}")
            file(READ "${XPACK_RELEASE_INFO}" XPACK_RELEASE_JSON)
            string(REGEX MATCH "\"tag_name\"[ \\t\\r\\n]*:[ \\t\\r\\n]*\"v([0-9]+\\.[0-9]+\\.[0-9]+-[0-9]+)\""
                _xpack_latest_match "${XPACK_RELEASE_JSON}")
            set(XPACK_LATEST_VERSION "${CMAKE_MATCH_1}")
            if(XPACK_LATEST_VERSION VERSION_GREATER XPACK_INSTALLED_VERSION)
                message(STATUS
                    "A newer xPack RISC-V GCC is available: ${XPACK_LATEST_VERSION} "
                    "(installed: ${XPACK_INSTALLED_VERSION}).")
            endif()
        else()
            message(STATUS "xPack RISC-V GCC update check skipped (GitHub unavailable).")
        endif()
    endif()
endif()

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_AR           "${TOOLCHAIN_PREFIX}ar")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_PREFIX}objcopy")
set(CMAKE_OBJDUMP      "${TOOLCHAIN_PREFIX}objdump")
set(CMAKE_SIZE         "${TOOLCHAIN_PREFIX}size")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Target RISC-V Architecture Flags: XuanTie E907 (RV32IMAC)
set(RISCV_ARCH_FLAGS "-march=rv32imac_zicsr_zifencei -mabi=ilp32 -mcmodel=medany")

set(CMAKE_C_FLAGS_INIT   "${RISCV_ARCH_FLAGS} -ffunction-sections -fdata-sections -Wall -Wextra")
set(CMAKE_CXX_FLAGS_INIT "${RISCV_ARCH_FLAGS} -std=c++20 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fcoroutines -ffunction-sections -fdata-sections -Wall -Wextra")
set(CMAKE_ASM_FLAGS_INIT "${RISCV_ARCH_FLAGS} -x assembler-with-cpp")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostartfiles -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs")
