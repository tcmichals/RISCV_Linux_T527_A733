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

// PIO interrupt trigger encoding, written to the INT_CFG nibble for the pin.
enum class IntTrigger : uint8_t {
    RISING_EDGE  = 0,
    FALLING_EDGE = 1,
    HIGH_LEVEL   = 2,
    LOW_LEVEL    = 3,
    BOTH_EDGES   = 4
};

class Gpio {
public:
    // Port indices below PORT_L select the main PIO; PORT_L and above select R_PIO.
    static constexpr uint32_t PORT_A = 0;
    static constexpr uint32_t PORT_B = 1;
    static constexpr uint32_t PORT_C = 2;
    static constexpr uint32_t PORT_J = 9;
    static constexpr uint32_t PORT_L = 16;
    static constexpr uint32_t PORT_M = 17;

    static constexpr uint32_t BANK_STRIDE = 0x30;

    struct PortRegs {
        volatile uint32_t CFG[4];  // 0x00-0x0C
        volatile uint32_t DATA;    // 0x10
        volatile uint32_t DRV[4];  // 0x14-0x20
        volatile uint32_t PULL[2]; // 0x24-0x28
    };

    struct alignas(32) IntRegs {
        volatile uint32_t CFG[4]; // 0x00-0x0C: Interrupt trigger mode
        volatile uint32_t CTL;    // 0x10: Interrupt Control / Enable
        volatile uint32_t STATUS; // 0x14: Interrupt Status (Write 1 to clear)
        volatile uint32_t DEBOUNCE;
    };

    // Atomic memory operations are not guaranteed on device memory, so the
    // default path masks interrupts instead. Define HAL_GPIO_USE_AMO only after
    // confirming the core supports AMOs on the PIO aperture.
    static inline void set_pin(uint32_t port_idx, uint32_t pin) noexcept {
        auto* port = get_port(port_idx);
#if defined(__riscv) && defined(__riscv_atomic) && defined(HAL_GPIO_USE_AMO)
        __asm__ volatile("amoor.w zero, %1, (%0)" :: "r"(&port->DATA), "r"(1U << pin) : "memory");
#else
        uint32_t flags = disable_interrupts();
        port->DATA |= (1U << pin);
        restore_interrupts(flags);
#endif
    }

    static inline void clear_pin(uint32_t port_idx, uint32_t pin) noexcept {
        auto* port = get_port(port_idx);
#if defined(__riscv) && defined(__riscv_atomic) && defined(HAL_GPIO_USE_AMO)
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
#if defined(__riscv) && defined(__riscv_atomic) && defined(HAL_GPIO_USE_AMO)
        __asm__ volatile("amoxor.w zero, %1, (%0)" :: "r"(&port->DATA), "r"(1U << pin) : "memory");
#else
        uint32_t flags = disable_interrupts();
        port->DATA ^= (1U << pin);
        restore_interrupts(flags);
#endif
    }

    static inline bool read_pin(uint32_t port_idx, uint32_t pin) noexcept {
        return (get_port(port_idx)->DATA & (1U << pin)) != 0U;
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

    // Direct ISR callback, for work that must not wait for the coroutine domain.
    // Runs in interrupt context: keep it short and non-blocking.
    using PinIsrCallback = void (*)(uint32_t port, uint32_t pin, void* context);

    static constexpr size_t MAX_PIN_HANDLERS = 8;

    static inline bool register_pin_isr(uint32_t port, uint32_t pin,
                                        PinIsrCallback callback, void* context,
                                        IntTrigger trigger = IntTrigger::RISING_EDGE) noexcept {
        const uint32_t flags = disable_interrupts();
        for (auto& slot : handlers_) {
            if (slot.callback != nullptr && (slot.port != port || slot.pin != pin)) {
                continue;
            }
            slot.port = port;
            slot.pin = pin;
            slot.context = context;
            slot.callback = callback;
            restore_interrupts(flags);
            enable_pin_interrupt(port, pin, trigger);
            return true;
        }
        restore_interrupts(flags);
        return false;
    }

    static inline void unregister_pin_isr(uint32_t port, uint32_t pin) noexcept {
        disable_pin_interrupt(port, pin);
        const uint32_t flags = disable_interrupts();
        for (auto& slot : handlers_) {
            if (slot.callback != nullptr && slot.port == port && slot.pin == pin) {
                slot.callback = nullptr;
                slot.context = nullptr;
            }
        }
        restore_interrupts(flags);
    }

    static inline void enable_pin_interrupt(uint32_t port_idx, uint32_t pin,
                                            IntTrigger trigger = IntTrigger::RISING_EDGE) noexcept {
        auto* int_regs = get_int_regs(port_idx);
        const uint32_t shift = (pin % 8U) * 4U;
        const uint32_t flags = disable_interrupts();
        uint32_t cfg = int_regs->CFG[pin / 8U];
        cfg &= ~(0x0FU << shift);
        cfg |= (static_cast<uint32_t>(trigger) & 0x0FU) << shift;
        int_regs->CFG[pin / 8U] = cfg;
        int_regs->STATUS = (1U << pin); // discard any edge latched while reconfiguring
        int_regs->CTL |= (1U << pin);
        restore_interrupts(flags);
    }

    static inline void disable_pin_interrupt(uint32_t port_idx, uint32_t pin) noexcept {
        auto* int_regs = get_int_regs(port_idx);
        const uint32_t flags = disable_interrupts();
        int_regs->CTL &= ~(1U << pin);
        int_regs->STATUS = (1U << pin);
        restore_interrupts(flags);
    }

    // Bank-level ISR entry point: a CLIC interrupt identifies the bank, not the pin.
    // Only bits this core enabled are acknowledged, so a bank shared with Linux
    // keeps its pending state intact.
    static inline void handle_bank_isr(uint32_t port) noexcept {
        auto* int_regs = get_int_regs(port);
        uint32_t pending = int_regs->STATUS & int_regs->CTL;
        if (pending == 0U) {
            return;
        }

        int_regs->STATUS = pending;

        while (pending != 0U) {
            const uint32_t pin = static_cast<uint32_t>(__builtin_ctz(pending));
            pending &= pending - 1U;
            dispatch_pin_event(port, pin);
        }
    }

    // Retained for callers that already know which pin fired.
    static inline void handle_isr(uint32_t port, uint32_t pin) noexcept {
        get_int_regs(port)->STATUS = (1U << pin);
        dispatch_pin_event(port, pin);
    }

private:
    struct PinHandler {
        PinIsrCallback callback;
        void* context;
        uint32_t port;
        uint32_t pin;
    };

    static inline void dispatch_pin_event(uint32_t port, uint32_t pin) noexcept {
        for (auto& slot : handlers_) {
            if (slot.callback != nullptr && slot.port == port && slot.pin == pin) {
                slot.callback(port, pin, slot.context);
                break;
            }
        }

        // Requests for other pins are rotated back so they keep waiting.
        const size_t queued = pending_pin_events_.size_from_isr();
        for (size_t i = 0; i < queued; ++i) {
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

    static inline PortRegs* get_port(uint32_t port_idx) noexcept {
        const uint32_t base = (port_idx >= PORT_L) ? R_PIO_BASE : PIO_BASE;
        const uint32_t bank = (port_idx >= PORT_L) ? (port_idx - PORT_L) : port_idx;
        return reinterpret_cast<PortRegs*>(base + (bank * BANK_STRIDE));
    }

    static inline IntRegs* get_int_regs(uint32_t port_idx) noexcept {
        const uint32_t base = (port_idx >= PORT_L) ? R_PIO_BASE : PIO_BASE;
        const uint32_t bank = (port_idx >= PORT_L) ? (port_idx - PORT_L) : port_idx;
        return reinterpret_cast<IntRegs*>(base + 0x200 + (bank * 0x20));
    }

    static inline void enable_pin_interrupt_default(uint32_t port_idx, uint32_t pin) noexcept {
        enable_pin_interrupt(port_idx, pin, IntTrigger::RISING_EDGE);
    }

    static inline uint32_t disable_interrupts() noexcept {
        return abstractx::disable_interrupts_save_flags();
    }

    static inline void restore_interrupts(uint32_t flags) noexcept {
        abstractx::restore_interrupts_flags(flags);
    }

    static inline DriverRequestQueue<PinEventRequest, 8> pending_pin_events_{};
    static inline PinHandler handlers_[MAX_PIN_HANDLERS]{};
};

} // namespace hal

#endif /* HAL_GPIO_HPP */
