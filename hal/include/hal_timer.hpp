/*
 * Hardware Timer HAL for RISC-V XuanTie E907 / E902
 * Directly interfaces with AbstractX GenericTimerService & IsrDispatcher.
 */

#ifndef HAL_TIMER_HPP
#define HAL_TIMER_HPP

#include <cstdint>
#include <coroutine>
#include "clock_config.hpp"
#include "abstractx/timer.hpp"
#include "abstractx/isr_dispatcher.hpp"

namespace hal {

class HardwareTimer {
public:
    static constexpr uint32_t CPU_FREQ_HZ = 600000000U; // Bring-up fallback only
    static constexpr uint32_t TICKS_PER_MS = CPU_FREQ_HZ / 1000U;

    static inline bool init_from_shared_clock_configuration(uint32_t tick_rate_hz = 1000) {
        const auto configuration = clocking::read_shared_configuration();
        if (!configuration.is_valid()) {
            return false;
        }

        init(tick_rate_hz,
             configuration.riscv_core_hz,
             configuration.timer_counter_hz);
        return true;
    }

    static inline void init(uint32_t tick_rate_hz = 1000,
                            uint32_t core_frequency_hz = CPU_FREQ_HZ,
                            uint32_t timer_counter_hz = CPU_FREQ_HZ) {
        if (tick_rate_hz == 0U || core_frequency_hz == 0U || timer_counter_hz == 0U) {
            return;
        }

        core_frequency_hz_ = core_frequency_hz;
        timer_counter_hz_ = timer_counter_hz;
        timer_interrupt_count_ = 0U;
        abstractx::GenericTimerService<32>::init();
        set_timer_period(timer_counter_hz_ / tick_rate_hz);
        enable_timer_interrupt();
    }

    static inline uint64_t get_cycles() noexcept {
#if defined(__riscv)
        uint32_t cycles_lo, cycles_hi, temp;
        do {
            __asm__ volatile("csrr %0, mcycleh" : "=r"(cycles_hi));
            __asm__ volatile("csrr %0, mcycle"  : "=r"(cycles_lo));
            __asm__ volatile("csrr %0, mcycleh" : "=r"(temp));
        } while (cycles_hi != temp);
        return (static_cast<uint64_t>(cycles_hi) << 32) | cycles_lo;
#else
        return 0;
#endif
    }

    static inline uint32_t get_time_us() noexcept {
        const uint64_t cycles = get_cycles();
        return static_cast<uint32_t>(
            (cycles / core_frequency_hz_) * 1000000U +
            ((cycles % core_frequency_hz_) * 1000000U) / core_frequency_hz_);
    }

    static inline uint32_t core_frequency_hz() noexcept { return core_frequency_hz_; }
    static inline uint32_t timer_counter_hz() noexcept { return timer_counter_hz_; }
    static inline uint64_t get_timer_ticks() noexcept { return read_mtime(); }
    static inline uint32_t timer_interrupt_count() noexcept { return timer_interrupt_count_; }

    // Called from RISC-V Timer Interrupt ISR (MTIME)
    static inline void handle_isr() noexcept {
        // Rearm timer for next tick
        arm_next_tick();
        ++timer_interrupt_count_;

        // Feed AbstractX timer wheel
        abstractx::GenericTimerService<32>::tick(1);
    }

    // Coroutine Awaiter helper for non-blocking sleep
    static inline auto sleep_ms(uint32_t ms) {
        return abstractx::GenericTimerService<32>::sleep_ms(ms);
    }

private:
    static inline uint32_t period_cycles_{CPU_FREQ_HZ / 1000U};
    static inline uint32_t core_frequency_hz_{CPU_FREQ_HZ};
    static inline uint32_t timer_counter_hz_{CPU_FREQ_HZ};
    static inline volatile uint32_t timer_interrupt_count_{0U};

    static inline void set_timer_period(uint32_t period) noexcept {
        period_cycles_ = period;
        arm_next_tick();
    }

    static inline void arm_next_tick() noexcept {
#if defined(__riscv)
        const uint64_t target = read_mtime() + period_cycles_;
        write_mtimecmp(target);
#endif
    }

    static inline uint64_t read_mtime() noexcept {
        const volatile uint32_t* const mtime =
            reinterpret_cast<const volatile uint32_t*>(RISCV_MTIME_ADDR);
        uint32_t high_before;
        uint32_t low;
        uint32_t high_after;
        do {
            high_before = mtime[1];
            low = mtime[0];
            high_after = mtime[1];
        } while (high_before != high_after);
        return (static_cast<uint64_t>(high_before) << 32U) | low;
    }

    static inline void write_mtimecmp(uint64_t target) noexcept {
        volatile uint32_t* const mtimecmp =
            reinterpret_cast<volatile uint32_t*>(RISCV_MTIMECMP_ADDR);
        // RV32 requires this sequence to avoid a transient early timer match.
        mtimecmp[1] = 0xFFFFFFFFU;
        mtimecmp[0] = static_cast<uint32_t>(target);
        mtimecmp[1] = static_cast<uint32_t>(target >> 32U);
    }

    static inline void enable_timer_interrupt() noexcept {
#if defined(__riscv)
        __asm__ volatile("csrs mie, %0" :: "r"(1 << 7)); // MTIE (Machine Timer Interrupt Enable)
        __asm__ volatile("csrs mstatus, %0" :: "r"(1 << 3)); // MIE (Global Interrupt Enable)
#endif
    }
};

} // namespace hal

#endif /* HAL_TIMER_HPP */
