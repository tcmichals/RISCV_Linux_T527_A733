/* Default RISC-V trap handler: CLIC dispatch plus a decoded crash report.
 *
 * On a fatal trap the cause is written to the RSC_TRACE buffer as text (visible
 * via /sys/kernel/debug/remoteproc/remoteprocN/trace0), the boot status record
 * is published for the Linux side, and the crash doorbell is rung so the
 * remoteproc driver can call rproc_report_crash().
 */

#include "hal_boot.hpp"
#include "hal_clic.hpp"
#include "hal_gpio.hpp"
#include "hal_msgbox.hpp"
#include "hal_timer.hpp"
#include "hal_trace.hpp"

namespace {

constexpr uint32_t kInterruptBit = 1U << 31U;
constexpr uint32_t kCauseMask = 0xFFFU;
constexpr uint32_t kMachineTimerInterruptCause = 7U;

uint32_t read_csr_mcause() noexcept {
#if defined(__riscv)
    uint32_t value;
    __asm__ volatile("csrr %0, mcause" : "=r"(value));
    return value;
#else
    return 0U;
#endif
}

uint32_t read_csr_mepc() noexcept {
#if defined(__riscv)
    uint32_t value;
    __asm__ volatile("csrr %0, mepc" : "=r"(value));
    return value;
#else
    return 0U;
#endif
}

uint32_t read_csr_mtval() noexcept {
#if defined(__riscv)
    uint32_t value;
    __asm__ volatile("csrr %0, mtval" : "=r"(value));
    return value;
#else
    return 0U;
#endif
}

uint32_t read_csr_mstatus() noexcept {
#if defined(__riscv)
    uint32_t value;
    __asm__ volatile("csrr %0, mstatus" : "=r"(value));
    return value;
#else
    return 0U;
#endif
}

const char* exception_name(uint32_t code) noexcept {
    switch (code) {
        case 0:  return "instruction address misaligned";
        case 1:  return "instruction access fault";
        case 2:  return "illegal instruction";
        case 3:  return "breakpoint";
        case 4:  return "load address misaligned";
        case 5:  return "load access fault";
        case 6:  return "store/AMO address misaligned";
        case 7:  return "store/AMO access fault";
        case 8:  return "ecall from U-mode";
        case 11: return "ecall from M-mode";
        default: return "unknown exception";
    }
}

// External interrupts arrive as CLIC IDs in mcause; route them to their driver.
void dispatch_external(uint32_t clic_id) noexcept {
    switch (static_cast<hal::ClicIrq>(clic_id)) {
        case hal::ClicIrq::GpioL_S:
        case hal::ClicIrq::GpioL_NS:
            hal::Gpio::handle_bank_isr(hal::Gpio::PORT_L);
            break;
        case hal::ClicIrq::GpioM_S:
        case hal::ClicIrq::GpioM_NS:
            hal::Gpio::handle_bank_isr(hal::Gpio::PORT_M);
            break;
        case hal::ClicIrq::RiscvMsgbox:
        case hal::ClicIrq::CpusMsgboxRiscv:
            hal::MsgboxDriver::handle_isr();
            break;
        default:
            // Unclaimed source: disable it rather than allow an interrupt storm.
            hal::Clic::disable(clic_id);
            hal::Trace::put_dec("[riscv] unhandled CLIC irq ", clic_id);
            break;
    }
}

[[noreturn]] void halt_after_crash() noexcept {
    while (true) {
#if defined(__riscv)
        __asm__ volatile("wfi");
#endif
    }
}

} // namespace

extern "C" void riscv_trap_handler() {
    const uint32_t mcause = read_csr_mcause();
    const uint32_t code = mcause & kCauseMask;

    if ((mcause & kInterruptBit) != 0U) {
        if (code == kMachineTimerInterruptCause) {
            hal::HardwareTimer::handle_isr();
        } else {
            dispatch_external(code);
        }
        return;
    }

    const uint32_t mepc = read_csr_mepc();
    const uint32_t mtval = read_csr_mtval();

    hal::Trace::puts("\n[riscv] FATAL TRAP: ");
    hal::Trace::puts(exception_name(code));
    hal::Trace::puts("\n");
    hal::Trace::put_hex32("[riscv]   mcause  = ", mcause);
    hal::Trace::put_hex32("[riscv]   mepc    = ", mepc);
    hal::Trace::put_hex32("[riscv]   mtval   = ", mtval);
    hal::Trace::put_hex32("[riscv]   mstatus = ", read_csr_mstatus());
    hal::Trace::puts("[riscv] halted, notifying host\n");

    hal::boot::Status status = hal::boot::make_status(
        hal::boot::State::Crashed,
        hal::boot::FailureReason::UnexpectedTrap,
        1U,
        0U,
        hal::HardwareTimer::core_frequency_hz());
    status.mcause = mcause;
    status.mepc = mepc;
    status.mtval = mtval;
    hal::boot::publish(status);

    hal::MsgboxDriver::ring_doorbell_to_linux(hal::boot::kFirmwareCrashDoorbell);
    halt_after_crash();
}
