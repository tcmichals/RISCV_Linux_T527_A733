/*
 * Core-Local Interrupt Controller (CLIC) driver for the Allwinner T527 E906.
 *
 * The E906 has no PLIC; every external interrupt arrives through the CLIC at
 * 0xE0800000. Main-domain peripherals (SPI0, UART2, ...) have no dedicated CLIC
 * line and reach the core only as grouped GIC interrupts, gated by the
 * GINTC_CONFIG registers in S_INTC. See docs/SOC_MEMORY_MAP_REFERENCE.md.
 */

#ifndef HAL_CLIC_HPP
#define HAL_CLIC_HPP

#include <cstddef>
#include <cstdint>

#include "memory_map.h"
#include "abstractx/interrupt_lock.hpp"

namespace hal {

// CLIC interrupt numbers for the sources this firmware owns (UM table 2-16).
enum class ClicIrq : uint32_t {
    RiscvWdt        = 16,
    RiscvMsgbox     = 17,
    McuTimer0       = 25,
    McuDmacNs       = 37,
    GpioL_S         = 62,
    GpioL_NS        = 63,
    GpioM_S         = 64,
    GpioM_NS        = 65,
    SUart0          = 66,
    SUart1          = 67,
    STwi0           = 68,
    STwi1           = 69,
    STwi2           = 60,
    SSpi            = 76,
    CpusMsgboxRiscv = 81,
    GicGroup32_39   = 90, // INT_SCRI[7]  - UART0..UART7
    GicGroup40_47   = 91, // INT_SCRI[8]
    GicGroup48_55   = 92, // INT_SCRI[9]  - SPI0..SPI2
    GicGroup56_63   = 93, // INT_SCRI[10]
};

enum class ClicTrigger : uint32_t {
    Level    = 0,
    PosEdge  = 1,
    NegEdge  = 3,
};

class Clic {
public:
    static constexpr uint32_t MAX_INTERRUPTS = 144U;

    // CLIC_INT_REGn bit fields, offset 0x1000 + n*4.
    static constexpr uint32_t INT_IP_SHIFT    = 0U;
    static constexpr uint32_t INT_IE_SHIFT    = 8U;
    static constexpr uint32_t INT_VECTOR_BIT  = 1U << 16U;
    static constexpr uint32_t INT_TRIG_SHIFT  = 17U;
    static constexpr uint32_t INT_TRIG_MASK   = 0x3U << INT_TRIG_SHIFT;
    static constexpr uint32_t INT_MODE_MASK   = 0x3U << 22U;
    static constexpr uint32_t INT_MODE_MACHINE = 0x3U << 22U;
    static constexpr uint32_t INT_PRIO_SHIFT  = 27U;
    static constexpr uint32_t INT_PRIO_MASK   = 0x1FU << INT_PRIO_SHIFT;

    static inline void init() noexcept {
        // NV is fixed at 1 (hardware vectoring supported); leave threshold open
        // so any enabled priority can preempt.
        reg(CFG_OFFSET) = 0U;
        reg(MINTTHRESH_OFFSET) = 0U;

        for (uint32_t i = 0; i < MAX_INTERRUPTS; ++i) {
            disable(i);
        }
    }

    static inline void enable(uint32_t irq, uint32_t priority = 1U,
                              ClicTrigger trigger = ClicTrigger::Level,
                              bool hardware_vectored = false) noexcept {
        if (irq >= MAX_INTERRUPTS) {
            return;
        }

        const uint32_t flags = abstractx::disable_interrupts_save_flags();
        uint32_t value = reg(int_offset(irq));

        value &= ~(INT_PRIO_MASK | INT_TRIG_MASK | INT_VECTOR_BIT);
        value |= INT_MODE_MACHINE;
        value |= (priority << INT_PRIO_SHIFT) & INT_PRIO_MASK;
        value |= (static_cast<uint32_t>(trigger) << INT_TRIG_SHIFT) & INT_TRIG_MASK;
        if (hardware_vectored) {
            value |= INT_VECTOR_BIT;
        }
        value |= (1U << INT_IE_SHIFT);

        reg(int_offset(irq)) = value;
        abstractx::restore_interrupts_flags(flags);
    }

    static inline void enable(ClicIrq irq, uint32_t priority = 1U,
                              ClicTrigger trigger = ClicTrigger::Level,
                              bool hardware_vectored = false) noexcept {
        enable(static_cast<uint32_t>(irq), priority, trigger, hardware_vectored);
    }

    static inline void disable(uint32_t irq) noexcept {
        if (irq >= MAX_INTERRUPTS) {
            return;
        }
        const uint32_t flags = abstractx::disable_interrupts_save_flags();
        reg(int_offset(irq)) &= ~(1U << INT_IE_SHIFT);
        abstractx::restore_interrupts_flags(flags);
    }

    static inline void disable(ClicIrq irq) noexcept {
        disable(static_cast<uint32_t>(irq));
    }

    static inline bool is_pending(uint32_t irq) noexcept {
        return irq < MAX_INTERRUPTS &&
               (reg(int_offset(irq)) & (1U << INT_IP_SHIFT)) != 0U;
    }

    // Only meaningful for pulse-mode sources; in level mode IP tracks the line.
    static inline void clear_pending(uint32_t irq) noexcept {
        if (irq >= MAX_INTERRUPTS) {
            return;
        }
        const uint32_t flags = abstractx::disable_interrupts_save_flags();
        reg(int_offset(irq)) &= ~(1U << INT_IP_SHIFT);
        abstractx::restore_interrupts_flags(flags);
    }

    static inline void clear_pending(ClicIrq irq) noexcept {
        clear_pending(static_cast<uint32_t>(irq));
    }

private:
    static constexpr uint32_t CFG_OFFSET        = 0x0000U;
    static constexpr uint32_t MINTTHRESH_OFFSET = 0x0008U;
    static constexpr uint32_t INT_BASE_OFFSET   = 0x1000U;

    static constexpr uint32_t int_offset(uint32_t irq) noexcept {
        return INT_BASE_OFFSET + (irq * 4U);
    }

    static inline volatile uint32_t& reg(uint32_t offset) noexcept {
        return *reinterpret_cast<volatile uint32_t*>(CLIC_BASE + offset);
    }
};

// Forwards a GIC interrupt group (8 sources) from the main domain to the CLIC.
// Needed for SPI0/UART2, which have no dedicated CLIC line.
class SharedGicGroup {
public:
    static constexpr uint32_t GINTC_CONFIG_REG0 = 0x00C0U;

    // gic_irq is the GIC SPI number, e.g. 48 for SPI0.
    static inline void enable_for_gic_irq(uint32_t gic_irq) noexcept {
        if (gic_irq < 32U) {
            return;
        }
        const uint32_t bit = gic_irq - 32U;
        const uint32_t reg_index = bit / 32U;
        const uint32_t flags = abstractx::disable_interrupts_save_flags();
        auto& r = reg(GINTC_CONFIG_REG0 + reg_index * 4U);
        r |= (1U << (bit % 32U));
        abstractx::restore_interrupts_flags(flags);
    }

    static inline void disable_for_gic_irq(uint32_t gic_irq) noexcept {
        if (gic_irq < 32U) {
            return;
        }
        const uint32_t bit = gic_irq - 32U;
        const uint32_t reg_index = bit / 32U;
        const uint32_t flags = abstractx::disable_interrupts_save_flags();
        auto& r = reg(GINTC_CONFIG_REG0 + reg_index * 4U);
        r &= ~(1U << (bit % 32U));
        abstractx::restore_interrupts_flags(flags);
    }

    // CLIC line carrying the 8-source group that contains gic_irq.
    static constexpr uint32_t clic_irq_for_gic_irq(uint32_t gic_irq) noexcept {
        return 90U + ((gic_irq - 32U) / 8U);
    }

private:
    static inline volatile uint32_t& reg(uint32_t offset) noexcept {
        return *reinterpret_cast<volatile uint32_t*>(S_INTC_BASE + offset);
    }
};

} // namespace hal

#endif /* HAL_CLIC_HPP */
