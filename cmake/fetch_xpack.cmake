# Automatic xPack RISC-V GCC installation for Linux hosts.
include(FetchContent)

set(XPACK_VERSION "15.2.0-1")
set(XPACK_URL "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v${XPACK_VERSION}/xpack-riscv-none-elf-gcc-${XPACK_VERSION}-linux-x64.tar.gz")

set(TOOLCHAIN_DOWNLOAD_DIR "$ENV{HOME}/.tools/xpack-riscv-none-elf-gcc-${XPACK_VERSION}")

if(NOT EXISTS "${TOOLCHAIN_DOWNLOAD_DIR}/bin/riscv-none-elf-gcc")
    message(STATUS "Downloading xPack RISC-V GCC toolchain to ${TOOLCHAIN_DOWNLOAD_DIR}...")
    file(MAKE_DIRECTORY "$ENV{HOME}/.tools")
    FetchContent_Declare(
        xpack_toolchain
        URL ${XPACK_URL}
        SOURCE_DIR ${TOOLCHAIN_DOWNLOAD_DIR}
    )
    FetchContent_MakeAvailable(xpack_toolchain)
endif()

set(RISCV_GCC_EXECUTABLE "${TOOLCHAIN_DOWNLOAD_DIR}/bin/riscv-none-elf-gcc" CACHE FILEPATH "RISC-V GCC executable" FORCE)

foreach(XPACK_TOOL gcc g++ ar objcopy objdump size)
    if(NOT EXISTS "${TOOLCHAIN_DOWNLOAD_DIR}/bin/riscv-none-elf-${XPACK_TOOL}")
        message(FATAL_ERROR
            "xPack installation is incomplete: missing riscv-none-elf-${XPACK_TOOL} in "
            "${TOOLCHAIN_DOWNLOAD_DIR}/bin. Delete that directory and configure again.")
    endif()
endforeach()
