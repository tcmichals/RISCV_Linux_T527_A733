/*
 * RSC_TRACE text logger.
 *
 * Writes plain text into the buffer advertised by the resource table, which
 * mainline remoteproc exposes at
 *   /sys/kernel/debug/remoteproc/remoteproc0/trace0
 *
 * Intended for boot progress and fatal traps: low rate, human readable, and
 * safe to call from an ISR. High-rate structured tracing belongs in BareCTF.
 */

#ifndef HAL_TRACE_HPP
#define HAL_TRACE_HPP

#include <cstddef>
#include <cstdint>

#include "memory_map.h"
#include "abstractx/interrupt_lock.hpp"

namespace hal {

class Trace {
public:
    static constexpr uint32_t BUFFER_ADDR = IPC_TRACE_BUFFER_ADDR;
    static constexpr size_t BUFFER_SIZE = IPC_TRACE_BUFFER_SIZE;

    // Wipe the buffer so a stale image from a previous boot is not mistaken for
    // this one, then announce ourselves.
    static inline void init() noexcept {
        volatile char* buf = buffer();
        for (size_t i = 0; i < BUFFER_SIZE; ++i) {
            buf[i] = '\0';
        }
        offset_ = 0U;
        wrapped_ = false;
        puts("[riscv] trace init\n");
    }

    static inline void puts(const char* text) noexcept {
        if (text == nullptr) {
            return;
        }
        const uint32_t flags = abstractx::disable_interrupts_save_flags();
        while (*text != '\0') {
            put_char_unlocked(*text++);
        }
        terminate_unlocked();
        abstractx::restore_interrupts_flags(flags);
    }

    static inline void put_hex32(const char* label, uint32_t value) noexcept {
        static const char digits[] = "0123456789abcdef";
        const uint32_t flags = abstractx::disable_interrupts_save_flags();
        if (label != nullptr) {
            while (*label != '\0') {
                put_char_unlocked(*label++);
            }
        }
        put_char_unlocked('0');
        put_char_unlocked('x');
        for (int shift = 28; shift >= 0; shift -= 4) {
            put_char_unlocked(digits[(value >> shift) & 0xFU]);
        }
        put_char_unlocked('\n');
        terminate_unlocked();
        abstractx::restore_interrupts_flags(flags);
    }

    static inline void put_dec(const char* label, uint32_t value) noexcept {
        char scratch[11];
        size_t len = 0;
        do {
            scratch[len++] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U && len < sizeof(scratch));

        const uint32_t flags = abstractx::disable_interrupts_save_flags();
        if (label != nullptr) {
            while (*label != '\0') {
                put_char_unlocked(*label++);
            }
        }
        while (len > 0U) {
            put_char_unlocked(scratch[--len]);
        }
        put_char_unlocked('\n');
        terminate_unlocked();
        abstractx::restore_interrupts_flags(flags);
    }

    static inline size_t used() noexcept { return offset_; }

private:
    static inline volatile char* buffer() noexcept {
        return reinterpret_cast<volatile char*>(BUFFER_ADDR);
    }

    // One byte is always held back for the terminator.
    static inline void put_char_unlocked(char c) noexcept {
        if (offset_ >= (BUFFER_SIZE - 1U)) {
            offset_ = 0U;
            wrapped_ = true;
        }
        buffer()[offset_++] = c;
    }

    static inline void terminate_unlocked() noexcept {
        buffer()[offset_] = '\0';
    }

    static inline size_t offset_{0};
    static inline bool wrapped_{false};
};

} // namespace hal

#endif /* HAL_TRACE_HPP */
