/*
 * Hardware Mailbox / Msgbox Doorbell Driver for Allwinner T527 & A733
 * Provides zero-contention IPI doorbell interrupts to/from Linux Host.
 * Integrates directly with AbstractX IsrDispatcher.
 */

#ifndef HAL_MSGBOX_HPP
#define HAL_MSGBOX_HPP

#include <cstdint>
#include <coroutine>
#include "memory_map.h"
#include "driver_request_queue.hpp"
#include "abstractx/isr_dispatcher.hpp"

namespace hal {

class MsgboxDriver {
public:
    struct alignas(32) Regs {
        volatile uint32_t CTRL[4];     // 0x00-0x0C: Channel Control
        volatile uint32_t IRQ_EN[4];   // 0x20-0x2C: Interrupt Enable
        volatile uint32_t IRQ_STAT[4]; // 0x40-0x4C: Interrupt Status
        volatile uint32_t MSG[8];      // 0x60-0x7C: Message Data FIFO
    };

    static inline void init() {
        auto* regs = get_regs();
        // Enable Channel 1 (Linux -> RISC-V) RX interrupt
        regs->IRQ_EN[1] = 0x01;
    }

    // Trigger doorbell interrupt to Linux Host
    static inline void ring_doorbell_to_linux(uint32_t message = 0x01) noexcept {
        auto* regs = get_regs();
#if defined(__riscv)
        __asm__ volatile("fence rw, rw" ::: "memory");
#endif
        regs->MSG[0] = message; // Write to Channel 0 (RISC-V -> Linux)
    }

    struct DoorbellRequest {
        std::coroutine_handle<> handle;
        uint32_t* message; // Points into the awaiter frame of the waiting coroutine.

        DoorbellRequest() noexcept : handle(nullptr), message(nullptr) {}
        DoorbellRequest(std::coroutine_handle<> h, uint32_t* msg) noexcept : handle(h), message(msg) {}
    };

    // Coroutine domain producer: interrupts are masked for the enqueue.
    static inline bool queue_doorbell_request(std::coroutine_handle<> handle, uint32_t* message) noexcept {
        return pending_doorbell_requests_.push(DoorbellRequest{handle, message});
    }

    // I/O domain consumer: called from the mailbox ISR, interrupts already masked.
    static inline bool pop_doorbell_request(DoorbellRequest& req) noexcept {
        return pending_doorbell_requests_.pop_from_isr(req);
    }

    /* Asynchronous Coroutine Awaiter for Host Doorbell Event */
    struct AsyncDoorbellAwaiter {
        uint32_t message{0};
        std::coroutine_handle<> handle{nullptr};

        bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            handle = h;
            queue_doorbell_request(h, &message);
        }

        uint32_t await_resume() const noexcept {
            return message;
        }
    };

    static inline AsyncDoorbellAwaiter wait_for_doorbell() noexcept {
        return AsyncDoorbellAwaiter();
    }

    // Called from Mailbox Interrupt ISR (IRQ 25)
    static inline void handle_isr() noexcept {
        auto* regs = get_regs();
        regs->IRQ_STAT[1] = 0x01; // Clear pending status
        const uint32_t msg = regs->MSG[1];

        DoorbellRequest req{};
        if (!pop_doorbell_request(req)) {
            return;
        }

        if (req.message != nullptr) {
            *req.message = msg;
        }
        abstractx::IsrDispatcher::post(req.handle);
    }

private:
    static inline Regs* get_regs() noexcept {
        return reinterpret_cast<Regs*>(MSGBOX_BASE);
    }

    static inline DriverRequestQueue<DoorbellRequest, 8> pending_doorbell_requests_{};
};

} // namespace hal

#endif /* HAL_MSGBOX_HPP */
