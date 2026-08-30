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
    // Registers are grouped per direction N (0-2), stride 0x100, with four
    // queues P (0-3) inside each group. See T527 UM 2.12.
    //   RISCV_MSGBOX: N=0 CPUS->RISCV, N=1 DSP->RISCV, N=2 CPUX->RISCV
    static constexpr uint32_t DIRECTION_STRIDE = 0x100U;
    static constexpr uint32_t RD_IRQ_EN     = 0x0020U;
    static constexpr uint32_t RD_IRQ_STATUS = 0x0024U;
    static constexpr uint32_t WR_IRQ_EN     = 0x0030U;
    static constexpr uint32_t WR_IRQ_STATUS = 0x0034U;
    static constexpr uint32_t DEBUG_REG     = 0x0040U;
    static constexpr uint32_t FIFO_STATUS   = 0x0050U;
    static constexpr uint32_t MSG_STATUS    = 0x0060U;
    static constexpr uint32_t MSG_QUEUE     = 0x0070U;
    static constexpr uint32_t WR_THRESHOLD  = 0x0080U;

    // Direction index used when receiving on RISCV_MSGBOX (CPUX -> RISCV).
    static constexpr uint32_t RX_DIRECTION = 2U;
    static constexpr uint32_t RX_QUEUE = 0U;

    static inline void init() {
        // Enable the read interrupt for the queue Linux writes into.
        reg(RISCV_MSGBOX_BASE, RD_IRQ_EN, RX_DIRECTION) = (1U << (RX_QUEUE * 2U));
    }

    // Trigger doorbell interrupt to Linux by writing the CPUX mailbox queue.
    static inline void ring_doorbell_to_linux(uint32_t message = 0x01) noexcept {
#if defined(__riscv)
        __asm__ volatile("fence rw, rw" ::: "memory");
#endif
        reg(MSGBOX_BASE, MSG_QUEUE, 0U, 0U) = message;
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

    // Called from Mailbox Interrupt ISR (CLIC 17)
    static inline void handle_isr() noexcept {
        const uint32_t status = reg(RISCV_MSGBOX_BASE, RD_IRQ_STATUS, RX_DIRECTION);
        if (status == 0U) {
            return;
        }

        const uint32_t msg = reg(RISCV_MSGBOX_BASE, MSG_QUEUE, RX_DIRECTION, RX_QUEUE);
        reg(RISCV_MSGBOX_BASE, RD_IRQ_STATUS, RX_DIRECTION) = status; // W1C

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
    static inline volatile uint32_t& reg(uint32_t base, uint32_t offset,
                                         uint32_t direction, uint32_t queue = 0U) noexcept {
        return *reinterpret_cast<volatile uint32_t*>(
            base + offset + (direction * DIRECTION_STRIDE) + (queue * 4U));
    }

    static inline DriverRequestQueue<DoorbellRequest, 8> pending_doorbell_requests_{};
};

} // namespace hal

#endif /* HAL_MSGBOX_HPP */
