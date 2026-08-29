/*
 * SPI DMA Driver (Dual-IO & Single-IO) for Allwinner T527 & A733
 * Supports Dual-IO Mode for FPGA TLPs & Single-IO Mode for IMU Transfers.
 * Fully integrated with AbstractX IsrDispatcher.
 */

#ifndef HAL_SPI_DMA_HPP
#define HAL_SPI_DMA_HPP

#include <cstdint>
#include <coroutine>
#include <span>
#include "memory_map.h"
#include "driver_request_queue.hpp"
#include "abstractx/isr_dispatcher.hpp"

namespace hal {

enum class SpiMode : uint8_t {
    SINGLE_IO = 0, // Standard 4-wire SPI (IMU, ADCs)
    DUAL_IO   = 1  // Dual-IO High Speed SPI (FPGA TLP Streaming)
};

class SpiDmaDriver {
public:
    struct alignas(32) Regs {
        volatile uint32_t GCR;     // 0x04: Global Control Register
        volatile uint32_t TCR;     // 0x08: Transfer Control Register
        volatile uint32_t IER;     // 0x10: Interrupt Enable Register
        volatile uint32_t ISR;     // 0x14: Interrupt Status Register
        volatile uint32_t FCR;     // 0x18: FIFO Control Register
        volatile uint32_t FSR;     // 0x1C: FIFO Status Register
        volatile uint32_t WCR;     // 0x20: Wait Clock Register
        volatile uint32_t CCR;     // 0x24: Clock Rate Control Register
        volatile uint32_t MBC;     // 0x30: Master Burst Counter
        volatile uint32_t MTC;     // 0x34: Master Transmit Counter
        volatile uint32_t BDC;     // 0x38: Master Burst Dummy Counter
        volatile uint32_t TXD;     // 0x200: TX Data Register
        volatile uint32_t RXD;     // 0x300: RX Data Register
    };

    static inline void init(uint32_t spi_base_addr, SpiMode mode = SpiMode::SINGLE_IO, uint32_t clock_hz = 25000000) {
        auto* regs = get_regs(spi_base_addr);
        (void)clock_hz;

        // Reset and enable Master Mode
        regs->GCR = 0x80000000; // Reset
        regs->GCR = 0x00000003; // Master mode, Module Enable

        // Set Single or Dual IO
        if (mode == SpiMode::DUAL_IO) {
            regs->TCR = (1 << 28) | (1 << 2); // Dual mode + Transmit Pause
        } else {
            regs->TCR = (1 << 2); // Standard single SPI
        }

        // Enable Transfer Complete (TC) Interrupt
        regs->IER = (1 << 12); // TC_INT_EN
    }

    struct TransferRequest {
        uint32_t spi_base;
        std::coroutine_handle<> handle;
        std::span<const uint8_t> tx_data;
        std::span<uint8_t> rx_data;

        TransferRequest() noexcept : spi_base(0), handle(nullptr), tx_data(), rx_data() {}
        TransferRequest(uint32_t base, std::coroutine_handle<> h, std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept
            : spi_base(base), handle(h), tx_data(tx), rx_data(rx) {}
    };

    using RequestQueue = DriverRequestQueue<TransferRequest, 8>;

    // Coroutine domain producer: interrupts are masked for the enqueue.
    static inline bool queue_transfer_request(uint32_t spi_base, std::coroutine_handle<> handle,
                                             std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
        return queue_for(spi_base).push(TransferRequest{spi_base, handle, tx, rx});
    }

    // I/O domain consumer: called from the SPI ISR, interrupts already masked.
    static inline bool pop_transfer_request(uint32_t spi_base, TransferRequest& req) noexcept {
        return queue_for(spi_base).pop_from_isr(req);
    }

    /* Asynchronous Full-Duplex / Burst Transfer Awaiter */
    struct AsyncTransferAwaiter {
        uint32_t spi_base;
        std::span<const uint8_t> tx_data;
        std::span<uint8_t> rx_data;
        std::coroutine_handle<> handle{nullptr};

        AsyncTransferAwaiter(uint32_t base, std::span<const uint8_t> tx, std::span<uint8_t> rx)
            : spi_base(base), tx_data(tx), rx_data(rx) {}

        bool await_ready() const noexcept {
            return tx_data.empty() && rx_data.empty();
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            handle = h;
            queue_transfer_request(spi_base, h, tx_data, rx_data);
            start_transfer(spi_base, tx_data, rx_data.size());
        }

        void await_resume() const noexcept {}
    };

    static inline AsyncTransferAwaiter async_transfer(uint32_t base, std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
        return AsyncTransferAwaiter(base, tx, rx);
    }

    // Called from SPI0 Interrupt ISR
    static inline void handle_spi0_isr() noexcept {
        complete_transfer_from_isr(SPI0_BASE);
    }

    // Called from SPI1 Interrupt ISR (IMU SPI)
    static inline void handle_spi1_isr() noexcept {
        complete_transfer_from_isr(SPI1_BASE);
    }

private:
    static inline RequestQueue& queue_for(uint32_t base) noexcept {
        return (base == SPI0_BASE) ? pending_spi0_requests_ : pending_spi1_requests_;
    }

    static inline void complete_transfer_from_isr(uint32_t base) noexcept {
        auto* regs = get_regs(base);
        regs->ISR = (1 << 12); // Clear TC flag

        TransferRequest req{};
        if (!pop_transfer_request(base, req)) {
            return;
        }

        for (size_t i = 0; i < req.rx_data.size(); ++i) {
            req.rx_data[i] = static_cast<uint8_t>(regs->RXD);
        }

        abstractx::IsrDispatcher::post(req.handle);
    }

    static inline Regs* get_regs(uint32_t base) noexcept {
        return reinterpret_cast<Regs*>(base);
    }

    static inline void start_transfer(uint32_t base, std::span<const uint8_t> tx, size_t rx_len) noexcept {
        auto* regs = get_regs(base);
        size_t total_len = tx.size() > rx_len ? tx.size() : rx_len;

        regs->MBC = static_cast<uint32_t>(total_len);
        regs->MTC = static_cast<uint32_t>(tx.size());
        regs->BDC = 0;

        for (auto b : tx) {
            regs->TXD = b;
        }

        // Start exchange (XCH)
        regs->TCR |= (1 << 31);
    }

    // One dedicated queue per bus instance: each SPI ISR is its own consumer.
    static inline RequestQueue pending_spi0_requests_{};
    static inline RequestQueue pending_spi1_requests_{};
};

} // namespace hal

#endif /* HAL_SPI_DMA_HPP */
