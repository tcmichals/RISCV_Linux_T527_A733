/*
 * TWI (I2C) driver for the Allwinner T527 MCU-domain controllers.
 *
 * Targets S_TWI0/1/2, which have dedicated CLIC lines (68/69/60) and so can be
 * serviced directly by the RISC-V core. Register offsets per T527 UM 8.x.
 *
 * Requests are queued from the coroutine domain and completed from the ISR,
 * matching the driver model in docs/IO_PROCESSOR_DRIVER_MODEL.md.
 */

#ifndef HAL_TWI_HPP
#define HAL_TWI_HPP

#include <cstddef>
#include <cstdint>
#include <coroutine>
#include <span>

#include "memory_map.h"
#include "driver_request_queue.hpp"
#include "abstractx/isr_dispatcher.hpp"

namespace hal {

enum class TwiResult : uint8_t { Pending, Ok, Nack, BusError };

struct TwiRequest {
    std::coroutine_handle<> handle{nullptr};
    uint8_t* buffer{nullptr};
    uint16_t length{0};
    uint8_t address{0};   // 7-bit
    uint8_t reg_addr{0};
    bool is_read{false};
    TwiResult* result{nullptr};
};

struct TwiTransferState {
    TwiRequest request{};
    uint16_t index{0};
    uint8_t address{0};
    uint8_t reg_addr{0};
    bool sent_reg_addr{false};
    bool phase_is_read{false};
    bool active{false};
};

class TwiDriver {
public:
    using Result = TwiResult;
    using Request = TwiRequest;
    struct Regs {
        volatile uint32_t ADDR;      // 0x00: Slave Address
        volatile uint32_t XADDR;     // 0x04: Extended Slave Address
        volatile uint32_t DATA;      // 0x08: Data Byte
        volatile uint32_t CNTR;      // 0x0C: Control
        volatile uint32_t STAT;      // 0x10: Status
        volatile uint32_t CCR;       // 0x14: Clock Control
        volatile uint32_t SRST;      // 0x18: Software Reset
        volatile uint32_t EFR;       // 0x1C: Enhance Feature
        volatile uint32_t LCR;       // 0x20: Line Control
    };

    static_assert(offsetof(Regs, CNTR) == 0x0C, "TWI_CNTR offset");
    static_assert(offsetof(Regs, LCR) == 0x20, "TWI_LCR offset");

    // TWI_CNTR bits
    static constexpr uint32_t CNTR_ACK    = 1U << 2;
    static constexpr uint32_t CNTR_INT_FLAG = 1U << 3;
    static constexpr uint32_t CNTR_M_STP  = 1U << 4;
    static constexpr uint32_t CNTR_M_STA  = 1U << 5;
    static constexpr uint32_t CNTR_BUS_EN = 1U << 6;
    static constexpr uint32_t CNTR_INT_EN = 1U << 7;

    // TWI_STAT values used by the master state machine
    static constexpr uint32_t STAT_BUS_ERROR      = 0x00;
    static constexpr uint32_t STAT_START          = 0x08;
    static constexpr uint32_t STAT_REPEAT_START   = 0x10;
    static constexpr uint32_t STAT_ADDR_W_ACK     = 0x18;
    static constexpr uint32_t STAT_ADDR_R_ACK     = 0x40;
    static constexpr uint32_t STAT_DATA_TX_ACK    = 0x28;
    static constexpr uint32_t STAT_DATA_RX_ACK    = 0x50;
    static constexpr uint32_t STAT_DATA_RX_NACK   = 0x58;
    static constexpr uint32_t STAT_IDLE           = 0xF8;

    enum class Unused : uint8_t { None };

    static inline void init(uint32_t base, uint32_t clk_div = 0x11) noexcept {
        auto* r = regs(base);
        r->SRST = 1U;                       // soft reset
        while ((r->SRST & 1U) != 0U) { }
        r->CCR = clk_div;                   // ~400 kHz from the 24 MHz source
        r->EFR = 0U;
        r->LCR = 0U;
        r->CNTR = CNTR_BUS_EN | CNTR_INT_EN;
    }

    // Coroutine domain producer.
    static inline bool queue_request(uint32_t base, const Request& req) noexcept {
        return queue_for(base).push(req);
    }

    // I/O domain consumer, called from the TWI ISR.
    static inline bool pop_request(uint32_t base, Request& req) noexcept {
        return queue_for(base).pop_from_isr(req);
    }

    struct AsyncTransferAwaiter {
        uint32_t base;
        Request request;
        Result result{Result::Pending};

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            request.handle = h;
            request.result = &result;
            queue_request(base, request);
            start_transfer(base);
        }

        Result await_resume() const noexcept { return result; }
    };

    static inline AsyncTransferAwaiter async_read(uint32_t base, uint8_t address,
                                                  uint8_t reg_addr,
                                                  std::span<uint8_t> dest) noexcept {
        AsyncTransferAwaiter awaiter{base, Request{}, Result::Pending};
        awaiter.request.address = address;
        awaiter.request.reg_addr = reg_addr;
        awaiter.request.buffer = dest.data();
        awaiter.request.length = static_cast<uint16_t>(dest.size());
        awaiter.request.is_read = true;
        return awaiter;
    }

    static inline AsyncTransferAwaiter async_write(uint32_t base, uint8_t address,
                                                   uint8_t reg_addr,
                                                   std::span<uint8_t> src) noexcept {
        AsyncTransferAwaiter awaiter{base, Request{}, Result::Pending};
        awaiter.request.address = address;
        awaiter.request.reg_addr = reg_addr;
        awaiter.request.buffer = src.data();
        awaiter.request.length = static_cast<uint16_t>(src.size());
        awaiter.request.is_read = false;
        return awaiter;
    }

    // Byte-at-a-time master state machine, advanced once per interrupt.
    static inline void handle_isr(uint32_t base) noexcept {
        auto* r = regs(base);
        if ((r->CNTR & CNTR_INT_FLAG) == 0U) {
            return;
        }

        auto& state = state_for(base);
        const uint32_t status = r->STAT;

        switch (status) {
            case STAT_START:
            case STAT_REPEAT_START:
                r->DATA = static_cast<uint32_t>(state.address << 1) |
                          (state.phase_is_read ? 1U : 0U);
                break;

            case STAT_ADDR_W_ACK:
                if (state.sent_reg_addr) {
                    write_next_byte(base, state);
                } else {
                    r->DATA = state.reg_addr;
                    state.sent_reg_addr = true;
                }
                break;

            case STAT_DATA_TX_ACK:
                if (state.request.is_read && state.sent_reg_addr && state.index == 0U) {
                    state.phase_is_read = true;
                    r->CNTR |= CNTR_M_STA; // repeated start for the read phase
                } else {
                    write_next_byte(base, state);
                }
                break;

            case STAT_ADDR_R_ACK:
                r->CNTR |= CNTR_ACK;
                break;

            case STAT_DATA_RX_ACK:
                read_next_byte(base, state);
                break;

            case STAT_DATA_RX_NACK:
                read_next_byte(base, state);
                finish(base, state, Result::Ok);
                return;

            case STAT_BUS_ERROR:
                finish(base, state, Result::BusError);
                return;

            default:
                finish(base, state, Result::Nack);
                return;
        }

        r->CNTR |= CNTR_INT_FLAG; // W1C: release the state machine
    }

private:
    using TransferState = TwiTransferState;

    static inline Regs* regs(uint32_t base) noexcept {
        return reinterpret_cast<Regs*>(base);
    }

    static inline DriverRequestQueue<Request, 8>& queue_for(uint32_t base) noexcept {
        return (base == S_TWI0_BASE) ? twi0_queue_
             : (base == S_TWI1_BASE) ? twi1_queue_
                                     : twi2_queue_;
    }

    static inline TransferState& state_for(uint32_t base) noexcept {
        return (base == S_TWI0_BASE) ? twi0_state_
             : (base == S_TWI1_BASE) ? twi1_state_
                                     : twi2_state_;
    }

    static inline void start_transfer(uint32_t base) noexcept {
        auto& state = state_for(base);
        if (state.active) {
            return; // ISR will pick up the next queued request on completion
        }
        Request req{};
        if (!queue_for(base).pop(req)) {
            return;
        }
        begin(base, state, req);
    }

    static inline void begin(uint32_t base, TransferState& state, const Request& req) noexcept {
        state.request = req;
        state.index = 0U;
        state.address = req.address;
        state.reg_addr = req.reg_addr;
        state.sent_reg_addr = false;
        state.phase_is_read = false;
        state.active = true;
        regs(base)->CNTR |= CNTR_M_STA;
    }

    static inline void write_next_byte(uint32_t base, TransferState& state) noexcept {
        if (state.index >= state.request.length) {
            finish(base, state, Result::Ok);
            return;
        }
        regs(base)->DATA = state.request.buffer[state.index++];
    }

    static inline void read_next_byte(uint32_t base, TransferState& state) noexcept {
        if (state.index < state.request.length) {
            state.request.buffer[state.index++] = static_cast<uint8_t>(regs(base)->DATA);
        }
        if ((state.index + 1U) >= state.request.length) {
            regs(base)->CNTR &= ~CNTR_ACK; // NACK the final byte
        }
    }

    static inline void finish(uint32_t base, TransferState& state, Result result) noexcept {
        auto* r = regs(base);
        r->CNTR |= CNTR_M_STP;
        r->CNTR |= CNTR_INT_FLAG;

        if (state.request.result != nullptr) {
            *state.request.result = result;
        }
        auto handle = state.request.handle;
        state.active = false;

        if (handle) {
            abstractx::IsrDispatcher::post(handle);
        }

        Request next{};
        if (queue_for(base).pop_from_isr(next)) {
            begin(base, state, next);
        }
    }

    static inline DriverRequestQueue<Request, 8> twi0_queue_{};
    static inline DriverRequestQueue<Request, 8> twi1_queue_{};
    static inline DriverRequestQueue<Request, 8> twi2_queue_{};
    static inline TransferState twi0_state_{};
    static inline TransferState twi1_state_{};
    static inline TransferState twi2_state_{};
};

} // namespace hal

#endif /* HAL_TWI_HPP */
