/*
 * Linux-to-firmware clock configuration ABI.
 * Linux writes this block before releasing the RemoteProc reset; firmware reads
 * it once during early initialization and never writes it.
 */

#ifndef HAL_CLOCK_CONFIG_HPP
#define HAL_CLOCK_CONFIG_HPP

#include <cstddef>
#include <cstdint>

#include "memory_map.h"

namespace hal::clocking {

constexpr uint32_t kClockConfigMagic = 0x4B4C4343U; // "CCLK" little-endian
constexpr uint32_t kClockConfigAbiVersion = 1U;

struct alignas(64) ClockConfiguration {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t size_bytes;
    uint32_t generation;
    uint32_t riscv_core_hz;
    uint32_t timer_counter_hz;
    uint32_t uart_parent_hz;
    uint32_t spi0_parent_hz;
    uint32_t spi1_parent_hz;
    uint32_t flags;
    uint32_t reserved[6];

    constexpr bool is_valid() const noexcept {
        return magic == kClockConfigMagic &&
               abi_version == kClockConfigAbiVersion &&
               size_bytes == sizeof(ClockConfiguration) &&
               generation != 0U &&
               riscv_core_hz != 0U &&
               timer_counter_hz != 0U;
    }
};

static_assert(sizeof(ClockConfiguration) == 64U);

inline ClockConfiguration read_shared_configuration() noexcept {
    ClockConfiguration configuration{};
    const volatile uint32_t* const source =
        reinterpret_cast<const volatile uint32_t*>(IPC_CLOCK_CONFIG_ADDR);
    uint32_t* const destination = reinterpret_cast<uint32_t*>(&configuration);

    for (size_t index = 0; index < sizeof(configuration) / sizeof(uint32_t); ++index) {
        destination[index] = source[index];
    }

    return configuration;
}

} // namespace hal::clocking

#endif /* HAL_CLOCK_CONFIG_HPP */
