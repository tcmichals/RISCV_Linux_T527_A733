/* Default RISC-V trap handler: timer dispatch plus best-effort crash report. */

#include "hal_boot.hpp"
#include "hal_msgbox.hpp"
#include "hal_timer.hpp"

namespace {

constexpr uint32_t kInterruptBit = 1U << 31U;
constexpr uint32_t kMachineTimerInterruptCause = 7U;

uint32_t read_mcause() noexcept {
#if defined(__riscv)
    uint32_t value;
    __asm__ volatile("csrr %0, mcause" : "=r"(value));
    return value;
#else
    return 0U;
#endif
}

uint32_t read_mepc() noexcept {
#if defined(__riscv)
    uint32_t value;
    __asm__ volatile("csrr %0, mepc" : "=r"(value));
    return value;
#else
    return 0U;
#endif
}

uint32_t read_mtval() noexcept {
#if defined(__riscv)
    uint32_t value;
    __asm__ volatile("csrr %0, mtval" : "=r"(value));
    return value;
#else
    return 0U;
#endif
}

[[noreturn]] void halt_after_crash() noexcept {
#if defined(__riscv)
    while (true) {
        __asm__ volatile("wfi");
    }
#else
    while (true) {
    }
#endif
}

} // namespace

extern "C" void riscv_trap_handler() {
    const uint32_t mcause = read_mcause();
    if ((mcause & kInterruptBit) != 0U &&
        (mcause & ~kInterruptBit) == kMachineTimerInterruptCause) {
        hal::HardwareTimer::handle_isr();
        return;
    }

    hal::boot::Status status = hal::boot::make_status(
        hal::boot::State::Crashed,
        hal::boot::FailureReason::UnexpectedTrap,
        1U,
        0U,
        hal::HardwareTimer::core_frequency_hz());
    status.mcause = mcause;
    status.mepc = read_mepc();
    status.mtval = read_mtval();
    hal::boot::publish(status);
    hal::MsgboxDriver::ring_doorbell_to_linux(hal::boot::kFirmwareCrashDoorbell);
    halt_after_crash();
}
