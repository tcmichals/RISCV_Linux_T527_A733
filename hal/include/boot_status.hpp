/* Firmware-to-Linux boot and crash status ABI. */

#ifndef HAL_BOOT_STATUS_HPP
#define HAL_BOOT_STATUS_HPP

#include <cstddef>
#include <cstdint>

#include "memory_map.h"

namespace hal::boot {

constexpr uint32_t kBootStatusMagic = 0x544F4F42U; // "BOOT" little-endian
constexpr uint32_t kBootStatusAbiVersion = 1U;
constexpr uint32_t kFirmwareReadyDoorbell = 0xA1U;
constexpr uint32_t kFirmwareInitFailedDoorbell = 0xA2U;
constexpr uint32_t kFirmwareCrashDoorbell = 0xA3U;

enum class State : uint32_t {
    Reset = 0,
    Initializing = 1,
    Ready = 2,
    InitFailed = 3,
    Crashed = 4,
};

enum class FailureReason : uint32_t {
    None = 0,
    ClockConfigurationInvalid = 1,
    TimerConfigurationInvalid = 2,
    UnexpectedTrap = 3,
    TimerCounterNotAdvancing = 4,
    TimerInterruptNotObserved = 5,
};

struct alignas(64) Status {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t size_bytes;
    uint32_t generation;
    uint32_t state;
    uint32_t failure_reason;
    uint32_t clock_configuration_generation;
    uint32_t riscv_core_hz;
    uint32_t mcause;
    uint32_t mepc;
    uint32_t mtval;
    uint32_t reserved[5];

    constexpr bool is_valid() const noexcept {
        return magic == kBootStatusMagic &&
               abi_version == kBootStatusAbiVersion &&
               size_bytes == sizeof(Status) &&
               generation != 0U &&
               state <= static_cast<uint32_t>(State::Crashed);
    }
};

static_assert(sizeof(Status) == 64U);

inline void publish(const Status& status) noexcept {
    volatile uint32_t* const destination =
        reinterpret_cast<volatile uint32_t*>(IPC_BOOT_STATUS_ADDR);
    const uint32_t* const source = reinterpret_cast<const uint32_t*>(&status);
    constexpr size_t kWords = sizeof(Status) / sizeof(uint32_t);

    // Commit generation last so Linux never accepts a partially-written record.
    for (size_t index = 0; index < kWords; ++index) {
        if (index != 3U) {
            destination[index] = source[index];
        }
    }
#if defined(__riscv)
    __asm__ volatile("fence rw, rw" ::: "memory");
#endif
    destination[3] = status.generation;
}

} // namespace hal::boot

#endif /* HAL_BOOT_STATUS_HPP */
