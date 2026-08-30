/*
 * Controlled fault test firmware for Allwinner T527 / A733 RemoteProc.
 *
 * Boots cleanly, announces itself in RSC_TRACE, then deliberately triggers a
 * known hardware trap (illegal instruction / unaligned store) to verify that:
 * 1. riscv_trap_handler intercepts the exception without crashing the core silently.
 * 2. mcause, mepc, mtval, and mstatus are decoded and printed to RSC_TRACE.
 * 3. hal::boot::Status is published with State::Crashed and FailureReason::UnexpectedTrap.
 * 4. hal::boot::kFirmwareCrashDoorbell (0xA3) is sent to Linux via the hardware mailbox.
 */

#include <cstdint>

#include "hal_boot.hpp"
#include "hal_trace.hpp"

extern "C" int main() {
    hal::Trace::init();
    hal::Trace::puts("[riscv] fault_test: boot successful, trace initialized\n");
    hal::Trace::puts("[riscv] fault_test: triggering controlled exception (unaligned store)\n");

#if defined(__riscv)
    // Write memory barrier to ensure trace buffer write is visible before fault
    __asm__ volatile("fence rw, rw" ::: "memory");

    // Trigger an unaligned store or illegal access fault
    volatile uint32_t* const unaligned_ptr = reinterpret_cast<volatile uint32_t*>(0x00000001U);
    *unaligned_ptr = 0xDEADBEEFU;

    // Fallback illegal instruction (unimp) in case unaligned stores are emulated or ignored
    __asm__ volatile(".word 0x00000000");
#endif

    while (true) {
#if defined(__riscv)
        __asm__ volatile("wfi");
#endif
    }

    return 0;
}
