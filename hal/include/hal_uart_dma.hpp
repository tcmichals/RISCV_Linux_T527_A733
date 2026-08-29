/*
 * UART DMA + Character Timeout (RTO) HAL Driver for Allwinner T527 & A733
 * Integrates with AbstractX IsrDispatcher for zero-copy async coroutines.
 */

#ifndef HAL_UART_DMA_HPP
#define HAL_UART_DMA_HPP

#include <cstdint>
#include <coroutine>
#include <span>
#include "memory_map.h"
#include "driver_request_queue.hpp"
#include "abstractx/isr_dispatcher.hpp"

namespace hal {

class UartDmaDriver {
public:
    static constexpr size_t RX_BUFFER_SIZE = 1024;
    static constexpr size_t TX_BUFFER_SIZE = 1024;

    struct alignas(32) Regs {
        volatile uint32_t RBR_THR_DLL; // 0x00
        volatile uint32_t DLH_IER;     // 0x04
        volatile uint32_t IIR_FCR;     // 0x08
        volatile uint32_t LCR;         // 0x0C
        volatile uint32_t MCR;         // 0x10
        volatile uint32_t LSR;         // 0x14
        volatile uint32_t MSR;         // 0x18
        volatile uint32_t SCH;         // 0x1C
    };

    static inline void init(uint32_t baudrate = 115200) {
        auto* regs = get_regs();
        
        // 1. Configure Baud Rate (Assuming 24MHz APB clock)
        uint32_t divisor = 24000000U / (16U * baudrate);
        regs->LCR = 0x80; // Enable DLAB
        regs->RBR_THR_DLL = divisor & 0xFF;
        regs->DLH_IER = (divisor >> 8) & 0xFF;
        regs->LCR = 0x03; // 8-N-1, DLAB = 0

        // 2. Enable FIFO and Reset RX/TX FIFOs
        regs->IIR_FCR = 0x07; // FIFO Enable + RX FIFO Reset + TX FIFO Reset

        // 3. Enable Receiver Data Available & Character Timeout (RTO) interrupts
        regs->DLH_IER = 0x01; // ERBFI: Enable Received Data Available / Timeout Interrupt
    }

    struct ReadRequest {
        std::coroutine_handle<> handle;
        std::span<uint8_t> buffer;
        size_t* bytes_received; // Points into the awaiter frame of the waiting coroutine.

        ReadRequest() noexcept : handle(nullptr), buffer(), bytes_received(nullptr) {}
        ReadRequest(std::coroutine_handle<> h, std::span<uint8_t> b, size_t* out) noexcept
            : handle(h), buffer(b), bytes_received(out) {}
    };

    // Coroutine domain producer: interrupts are masked for the enqueue.
    static inline bool queue_read_request(std::coroutine_handle<> handle, std::span<uint8_t> buffer, size_t* bytes_received) noexcept {
        return pending_read_requests_.push(ReadRequest{handle, buffer, bytes_received});
    }

    // I/O domain consumer: called from the UART ISR, interrupts already masked.
    static inline bool pop_read_request(ReadRequest& req) noexcept {
        return pending_read_requests_.pop_from_isr(req);
    }

    /* Asynchronous Coroutine Read Awaiter */
    struct AsyncReadAwaiter {
        std::span<uint8_t> dest_buffer;
        size_t bytes_transferred{0};
        std::coroutine_handle<> handle{nullptr};

        explicit AsyncReadAwaiter(std::span<uint8_t> buf) : dest_buffer(buf) {}

        bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            handle = h;
            queue_read_request(h, dest_buffer, &bytes_transferred);
        }

        size_t await_resume() const noexcept {
            return bytes_transferred;
        }
    };

    static inline AsyncReadAwaiter async_read(std::span<uint8_t> buf) noexcept {
        return AsyncReadAwaiter(buf);
    }

    /* Asynchronous Coroutine Write Awaiter */
    struct AsyncWriteAwaiter {
        std::span<const uint8_t> src_data;
        std::coroutine_handle<> handle{nullptr};

        explicit AsyncWriteAwaiter(std::span<const uint8_t> data) : src_data(data) {}

        bool await_ready() const noexcept {
            return src_data.empty();
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            handle = h;
            tx_awaiting_handle_ = handle;
            start_tx_dma(src_data);
        }

        void await_resume() const noexcept {}
    };

    static inline AsyncWriteAwaiter async_write(std::span<const uint8_t> data) noexcept {
        return AsyncWriteAwaiter(data);
    }

    // Called from UART ISR (Handles Character Timeout 0b1100 and RX Data)
    static inline void handle_isr() noexcept {
        auto* regs = get_regs();
        uint32_t iir = regs->IIR_FCR & 0x0F;

        if (iir == 0x04 || iir == 0x0C) { // 0x04: Data Available, 0x0C: Character Timeout (RTO)
            ReadRequest req{};
            if (!pop_read_request(req)) {
                while (regs->LSR & 0x01) { // No waiter: flush the FIFO so the IRQ deasserts.
                    (void)regs->RBR_THR_DLL;
                }
                return;
            }

            size_t count = 0;
            while ((regs->LSR & 0x01) && (count < req.buffer.size())) {
                req.buffer[count++] = static_cast<uint8_t>(regs->RBR_THR_DLL);
            }

            if (req.bytes_received != nullptr) {
                *req.bytes_received = count;
            }
            abstractx::IsrDispatcher::post(req.handle);
        }
    }

    // Called from DMA TX Completion ISR
    static inline void handle_tx_dma_isr() noexcept {
        if (tx_awaiting_handle_) {
            auto h = tx_awaiting_handle_;
            tx_awaiting_handle_ = nullptr;
            abstractx::IsrDispatcher::post(h);
        }
    }

private:
    static inline Regs* get_regs() noexcept {
        return reinterpret_cast<Regs*>(UART2_BASE);
    }

    static inline void start_tx_dma(std::span<const uint8_t> data) noexcept {
        auto* regs = get_regs();
        for (auto b : data) {
            while (!(regs->LSR & 0x20)) { /* Spin for TX FIFO ready in simulation/fallback */ }
            regs->RBR_THR_DLL = b;
        }
        handle_tx_dma_isr();
    }

    static inline DriverRequestQueue<ReadRequest, 8> pending_read_requests_{};
    static inline std::coroutine_handle<> tx_awaiting_handle_{nullptr};
};

} // namespace hal

#endif /* HAL_UART_DMA_HPP */
