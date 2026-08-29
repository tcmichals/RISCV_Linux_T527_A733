/* Firmware boot handshake helpers for Linux RemoteProc. */

#ifndef HAL_BOOT_HPP
#define HAL_BOOT_HPP

#include <cstdint>

#include "boot_status.hpp"
#include "clock_config.hpp"
#include "hal_msgbox.hpp"
#include "hal_timer.hpp"

namespace hal::boot {

inline Status make_status(State state,
                          FailureReason failure_reason,
                          uint32_t generation,
                          uint32_t clock_configuration_generation = 0U,
                          uint32_t riscv_core_hz = 0U) noexcept {
    return Status{
        .magic = kBootStatusMagic,
        .abi_version = kBootStatusAbiVersion,
        .size_bytes = sizeof(Status),
        .generation = generation,
        .state = static_cast<uint32_t>(state),
        .failure_reason = static_cast<uint32_t>(failure_reason),
        .clock_configuration_generation = clock_configuration_generation,
        .riscv_core_hz = riscv_core_hz,
        .mcause = 0U,
        .mepc = 0U,
        .mtval = 0U,
        .reserved = {},
    };
}

inline bool initialize_from_linux_clock_configuration(uint32_t timer_tick_hz = 1000U) noexcept {
    const auto configuration = clocking::read_shared_configuration();
    const uint32_t status_generation = configuration.generation == 0U ? 1U : configuration.generation;

    if (!configuration.is_valid()) {
        publish(make_status(State::InitFailed,
                            FailureReason::ClockConfigurationInvalid,
                            status_generation));
        MsgboxDriver::ring_doorbell_to_linux(kFirmwareInitFailedDoorbell);
        return false;
    }

    if (timer_tick_hz == 0U || configuration.timer_counter_hz < timer_tick_hz) {
        publish(make_status(State::InitFailed,
                            FailureReason::TimerConfigurationInvalid,
                            status_generation,
                            configuration.generation,
                            configuration.riscv_core_hz));
        MsgboxDriver::ring_doorbell_to_linux(kFirmwareInitFailedDoorbell);
        return false;
    }

    HardwareTimer::init(timer_tick_hz,
                        configuration.riscv_core_hz,
                        configuration.timer_counter_hz);
    publish(make_status(State::Ready,
                        FailureReason::None,
                        status_generation,
                        configuration.generation,
                        configuration.riscv_core_hz));
    MsgboxDriver::ring_doorbell_to_linux(kFirmwareReadyDoorbell);
    return true;
}

} // namespace hal::boot

#endif /* HAL_BOOT_HPP */
