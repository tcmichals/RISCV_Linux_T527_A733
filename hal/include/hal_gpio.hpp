/*
 * ISR-Safe GPIO / PIO Driver for Allwinner T527 & A733
 * Provides atomic pin toggle/set/clear for profiling/CPU measurement
 * and edge-triggered interrupt dispatch to AbstractX coroutines.
 */

#ifndef HAL_GPIO_HPP
#define HAL_GPIO_HPP

#include <cstdint>
#include <coroutine>
#include <atomic>
#include "memory_map.h"
#include "driver_request_queue.hpp"
#include "abstractx/isr_dispatcher.hpp"

namespace hal {

enum class PinMode : uint8_t {
    INPUT  = 0,
    OUTPUT = 1,
    INT_RISING = 4,
    INT_FALLING = 5,
    INT_BOTH = 6
};

class Gpio {
public:
    struct alignas(32) PortRegs {
        volatile uint32_t CFG[4]; // 0x00-0x0C: Configure registers (4 bits per pin)
        volatile uint32_t DATA;   // 0x10: Data register (1 bit per pin)
        volatile uint32_t DRV[2]; // 0x14-0x18: Multi-driving
        volatile uint32_t PULL[2];// 0x1C-0x20: Pull up/down
    };

    struct alignas(32) IntRegs {
        volatile uint32_t CFG[4]; // 0x00-0x0C: Interrupt trigger mode
        volatile uint32_t CTL;    // 0x10: Interrupt Control / Enable
        volatile uint32_t STATUS; // 0x14: Interrupt Status (Write 1 to clear)
        volatile uint32_t DEBOUNCE;
    };

    // Fast, Zero-Contention ISR-Safe Pin Manipulation
    static inline void set_pin(uint32_t port_idx, uint32_t pin) noexcept {
        auto* port = get_port(port_idx);
#if defined(__riscv) && defined(__riscv_atomic)
        // Atomic OR instruction on memory (amoadd / amoor)
        __asm__ volatile("amoor.w zero, %1, (%0)" :: "r"(&port->DATA), "r"(1U << pin) : "memory");
#else
        uint32_t flags = disable_interrupts();
        port->DATA |= (1U << pin);
        restore_interrupts(flags);
#endif
    }

    static inline void clear_pin(uint32_t port_idx, uint32_t pin) noexcept {
        auto* port = get_port(port_idx);
#if defined(__riscv) && defined(__riscv_atomic)
        __asm__ volatile("amoand.w zero, %1, (%0)" :: "r"(&port->DATA), "r"(~(1U << pin)) : "memory");
#else
        uint32_t flags = disable_interrupts();
        port->DATA &= ~(1U << pin);
        restore_interrupts(flags);
#endif
    }

    // Atomic Pin Toggle for measuring CPU/ISR execution time with oscilloscope/logic analyzer
    static inline void toggle_pin(uint32_t port_idx, uint32_t pin) noexcept {
        auto* port = get_port(port_idx);
#if defined(__riscv) && defined(__riscv_atomic)
        __asm__ volatile("amoxor.w zero, %1, (%0)" :: "r"(&port->DATA), "r"(1U << pin) : "memory");
#else
        uint32_t flags = disable_interrupts();
        port->DATA ^= (1U << pin);
        restore_interrupts(flags);
#endif
    }

    static inline void configure_pin(uint32_t port_idx, uint32_t pin, PinMode mode) noexcept {
        auto* port = get_port(port_idx);
        uint32_t reg_idx = pin / 8;
        uint32_t bit_offset = (pin % 8) * 4;

        uint32_t val = port->CFG[reg_idx];
        val &= ~(0x0FU << bit_offset);
        val |= (static_cast<uint32_t>(mode) & 0x0FU) << bit_offset;
        port->CFG[reg_idx] = val;
    }

    struct PinEventRequest {
        std::coroutine_handle<> handle;
        uint32_t port;
        uint32_t pin;

        PinEventRequest() noexcept : handle(nullptr), port(0), pin(0) {}
        PinEventRequest(std::coroutine_handle<> h, uint32_t p, uint32_t n) noexcept : handle(h), port(p), pin(n) {}
    };

    // Coroutine domain producer: interrupts are masked for the enqueue.
    static inline bool queue_pin_event_request(std::coroutine_handle<> handle, uint32_t port, uint32_t pin) noexcept {
        return pending_pin_events_.push(PinEventRequest{handle, port, pin});
    }

    // I/O domain consumer: called from the GPIO ISR, interrupts already masked.
    static inline bool pop_pin_event_request(PinEventRequest& req) noexcept {
        return pending_pin_events_.pop_from_isr(req);
    }

    /* Asynchronous Pin Interrupt Coroutine Awaiter (e.g. IMU DRDY) */
    struct AsyncPinEventAwaiter {
        uint32_t port;
        uint32_t pin;
        std::coroutine_handle<> handle{nullptr};

        AsyncPinEventAwaiter(uint32_t p, uint32_t pin_num) : port(p), pin(pin_num) {}

        bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            handle = h;
            queue_pin_event_request(h, port, pin);
            enable_pin_interrupt(port, pin);
        }

        void await_resume() const noexcept {}
    };

    static inline AsyncPinEventAwaiter wait_for_edge(uint32_t port, uint32_t pin) noexcept {
        return AsyncPinEventAwaiter(port, pin);
    }

    // Called from GPIO Interrupt ISR
    static inline void handle_isr(uint32_t port, uint32_t pin) noexcept {
        auto* int_regs = get_int_regs(port);
        int_regs->STATUS = (1U << pin); // Write 1 to clear

        // Requests for other pins are rotated back so they keep waiting.
        const size_t pending = pending_pin_events_.size_from_isr();
        for (size_t i = 0; i < pending; ++i) {
            PinEventRequest req{};
            if (!pending_pin_events_.pop_from_isr(req)) {
                return;
            }
            if (req.port == port && req.pin == pin) {
                abstractx::IsrDispatcher::post(req.handle);
                return;
            }
            pending_pin_events_.push_from_isr(req);
        }
    }

private:
    static inline PortRegs* get_port(uint32_t port_idx) noexcept {
        return reinterpret_cast<PortRegs*>(PIO_BASE + (port_idx * 0x30));
    }

    static inline IntRegs* get_int_regs(uint32_t port_idx) noexcept {
        return reinterpret_cast<IntRegs*>(PIO_BASE + 0x200 + (port_idx * 0x20));
    }

    static inline void enable_pin_interrupt(uint32_t port_idx, uint32_t pin) noexcept {
        auto* int_regs = get_int_regs(port_idx);
        int_regs->CFG[pin / 8] |= (0x01U << ((pin % 8) * 4)); // Positive edge trigger
        int_regs->CTL |= (1U << pin);                          // Enable interrupt
    }

    static inline uint32_t disable_interrupts() noexcept {
        return abstractx::disable_interrupts_save_flags();
    }

    static inline void restore_interrupts(uint32_t flags) noexcept {
        abstractx::restore_interrupts_flags(flags);
    }

    static inline DriverRequestQueue<PinEventRequest, 8> pending_pin_events_{};
};

} // namespace hal

#endif /* HAL_GPIO_HPP */
