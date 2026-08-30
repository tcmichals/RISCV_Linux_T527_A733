/*
 * SPI DMA Driver (Dual-IO & Single-IO) for Allwinner T527 & A733
 * Supports Dual-IO Mode for FPGA TLPs & Single-IO Mode for IMU Transfers.
 * Fully integrated with AbstractX IsrDispatcher.
 */

#ifndef HAL_SPI_DMA_HPP
#define HAL_SPI_DMA_HPP

#include <cstddef>
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
    // Offsets per T527 UM 8.x SPI register list. Padding is explicit because the
    // register file is sparse: TXD is at 0x200 and RXD at 0x300.
    struct Regs {
        volatile uint32_t RESERVED0;  // 0x00
        volatile uint32_t GCR;        // 0x04: Global Control
        volatile uint32_t TCR;        // 0x08: Transfer Control
        volatile uint32_t RESERVED1;  // 0x0C
        volatile uint32_t IER;        // 0x10: Interrupt Control
        volatile uint32_t ISR;        // 0x14: Interrupt Status
        volatile uint32_t FCR;        // 0x18: FIFO Control
        volatile uint32_t FSR;        // 0x1C: FIFO Status
        volatile uint32_t WCR;        // 0x20: Wait Clock Counter
        volatile uint32_t CCR;        // 0x24: Clock Control Register
        volatile uint32_t SAMP_DL;    // 0x28: Sample Delay Control
        volatile uint32_t RESERVED3;  // 0x2C
        volatile uint32_t MBC;        // 0x30: Master Burst Counter
        volatile uint32_t MTC;        // 0x34: Master Transmit Counter
        volatile uint32_t BCC;        // 0x38: Master Burst Control Counter
        volatile uint32_t RESERVED4;  // 0x3C
        volatile uint32_t BATCR;      // 0x40: Bit-Aligned Transfer Config
        volatile uint32_t RESERVED5;  // 0x44
        volatile uint32_t TBR;        // 0x48: TX Bit
        volatile uint32_t RBR;        // 0x4C: RX Bit
        volatile uint32_t RESERVED6[14]; // 0x50-0x87
        volatile uint32_t NDMA_MODE_CTL; // 0x88: Normal DMA Mode Control
        volatile uint32_t RESERVED7[93]; // 0x8C-0x1FF
        volatile uint32_t TXD;        // 0x200: TX Data
        volatile uint32_t RESERVED8[63]; // 0x204-0x2FF
        volatile uint32_t RXD;        // 0x300: RX Data
        volatile uint32_t RESERVED9[63]; // 0x304-0x3FF
        volatile uint32_t BSR;        // 0x400: BUF Status
    };

    static_assert(offsetof(Regs, GCR) == 0x04, "SPI_GCR offset");
    static_assert(offsetof(Regs, ISR) == 0x14, "SPI_ISR offset");
    static_assert(offsetof(Regs, CCR) == 0x24, "SPI_CCR offset");
    static_assert(offsetof(Regs, MBC) == 0x30, "SPI_MBC offset");
    static_assert(offsetof(Regs, TXD) == 0x200, "SPI_TXD offset");
    static_assert(offsetof(Regs, RXD) == 0x300, "SPI_RXD offset");
    static_assert(offsetof(Regs, BSR) == 0x400, "SPI_BSR offset");

    static inline void set_frequency(uint32_t spi_base_addr, uint32_t target_hz, uint32_t parent_hz = 200000000U) noexcept {
        if (target_hz == 0U) {
            return;
        }
        if (parent_hz == 0U) {
            parent_hz = 200000000U;
        }
        auto* regs = get_regs(spi_base_addr);

        // Mode 1: CDR2 (linear divider: SCLK = parent / (2 * (CDR2 + 1)))
        const uint32_t div = parent_hz / (2U * target_hz);
        if (div >= 1U && div <= 256U) {
            regs->CCR = (div - 1U); // DRS=0, CDR2=div-1
        } else {
            // Mode 2: CDR1 (power of 2: SCLK = parent / (2^CDR1))
            uint32_t cdr1 = 0;
            uint32_t cur = parent_hz;
            while (cur > target_hz && cdr1 < 15U) {
                cur >>= 1U;
                cdr1++;
            }
            regs->CCR = (1U << 12) | ((cdr1 & 0x0FU) << 8);
        }
    }

    static inline void init(uint32_t spi_base_addr, SpiMode mode = SpiMode::SINGLE_IO,
                            uint32_t target_hz = 25000000U, uint32_t parent_hz = 200000000U) {
        auto* regs = get_regs(spi_base_addr);

        // Reset and enable Master Mode
        regs->GCR = 0x80000000; // Reset
        regs->GCR = 0x00000003; // Master mode, Module Enable

        // Set Single or Dual IO
        if (mode == SpiMode::DUAL_IO) {
            regs->TCR = (1 << 28) | (1 << 2); // Dual mode + Transmit Pause
        } else {
            regs->TCR = (1 << 2); // Standard single SPI
        }

        set_frequency(spi_base_addr, target_hz, parent_hz);

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
            try_start_next_transfer(spi_base);
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

    static inline TransferRequest& active_request_for(uint32_t base) noexcept {
        return (base == SPI0_BASE) ? active_spi0_request_ : active_spi1_request_;
    }

    static inline bool& busy_for(uint32_t base) noexcept {
        return (base == SPI0_BASE) ? busy_spi0_ : busy_spi1_;
    }

    static inline void try_start_next_transfer(uint32_t base) noexcept {
        const uint32_t flags = abstractx::disable_interrupts_save_flags();
        bool& busy = busy_for(base);
        if (!busy) {
            TransferRequest req{};
            if (pop_transfer_request(base, req)) {
                busy = true;
                active_request_for(base) = req;
                start_transfer(base, req.tx_data, req.rx_data.size());
            }
        }
        abstractx::restore_interrupts_flags(flags);
    }

    static inline void complete_transfer_from_isr(uint32_t base) noexcept {
        auto* regs = get_regs(base);
        regs->ISR = (1 << 12); // Clear TC flag

        TransferRequest req = active_request_for(base);
        for (size_t i = 0; i < req.rx_data.size(); ++i) {
            req.rx_data[i] = static_cast<uint8_t>(regs->RXD);
        }

        bool& busy = busy_for(base);
        TransferRequest next_req{};
        if (pop_transfer_request(base, next_req)) {
            busy = true;
            active_request_for(base) = next_req;
            start_transfer(base, next_req.tx_data, next_req.rx_data.size());
        } else {
            busy = false;
            active_request_for(base) = TransferRequest{};
        }

        if (req.handle) {
            abstractx::IsrDispatcher::post(req.handle);
        }
    }

    static inline Regs* get_regs(uint32_t base) noexcept {
        return reinterpret_cast<Regs*>(base);
    }

    static inline void start_transfer(uint32_t base, std::span<const uint8_t> tx, size_t rx_len) noexcept {
        auto* regs = get_regs(base);
        size_t total_len = tx.size() > rx_len ? tx.size() : rx_len;

        regs->MBC = static_cast<uint32_t>(total_len);
        regs->MTC = static_cast<uint32_t>(tx.size());
        regs->BCC = 0;

        for (auto b : tx) {
            regs->TXD = b;
        }

        // Start exchange (XCH)
        regs->TCR |= (1 << 31);
    }

    // Dedicated request queues and active state per bus instance
    static inline RequestQueue pending_spi0_requests_{};
    static inline RequestQueue pending_spi1_requests_{};
    static inline TransferRequest active_spi0_request_{};
    static inline TransferRequest active_spi1_request_{};
    static inline bool busy_spi0_{false};
    static inline bool busy_spi1_{false};
};

} // namespace hal

#endif /* HAL_SPI_DMA_HPP */
