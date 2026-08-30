/*
 * Bidirectional IPC Ping-Pong Test firmware for T527 / A733 RemoteProc.
 *
 * Demonstrates:
 * 1. Listening for Linux-to-RISC-V IPC ping packets in SRAM C (g_rx_ring).
 * 2. Echoing received descriptors back to Linux in SRAM C (g_tx_ring) with firmware timestamps.
 * 3. Ringing the mailbox doorbell to alert the Linux host.
 * 4. Reporting round-trip ping counts and status through RSC_TRACE.
 */

#include <cstdint>
#include <cstdio>

#include "abstractx/coro.hpp"
#include "abstractx/isr_dispatcher.hpp"
#include "abstractx/timer.hpp"
#include "hal_boot.hpp"
#include "hal_msgbox.hpp"
#include "hal_timer.hpp"
#include "hal_trace.hpp"
#include "ipc_protocol.hpp"

// SPSC Rings mapped into SRAM C (.spsc_descriptors)
__attribute__((section(".spsc_descriptors")))
abstractx::ipc::SpscRingControl g_tx_ring;

__attribute__((section(".spsc_descriptors")))
abstractx::ipc::SpscRingControl g_rx_ring;

// Echo payload buffers in DRAM (.dram_buffers)
__attribute__((section(".dram_buffers")))
uint8_t g_dram_echo_pool[128][256];

abstractx::Task<void> ipc_ping_responder_coro() {
    uint32_t echo_count = 0;

    while (true) {
        // Check if Linux has produced a packet on g_rx_ring
        uint32_t rx_head = g_rx_ring.head.load(std::memory_order_acquire);
        uint32_t rx_tail = g_rx_ring.tail.load(std::memory_order_relaxed);

        while (rx_tail != rx_head) {
            uint32_t rx_slot = rx_tail & abstractx::ipc::RING_MASK;
            const auto& in_desc = g_rx_ring.descriptors[rx_slot];

            // Produce response on g_tx_ring
            uint32_t tx_head = g_tx_ring.head.load(std::memory_order_relaxed);
            uint32_t tx_tail = g_tx_ring.tail.load(std::memory_order_acquire);

            if ((tx_head - tx_tail) < abstractx::ipc::RING_SIZE) {
                uint32_t tx_slot = tx_head & abstractx::ipc::RING_MASK;
                uint8_t* tx_payload = g_dram_echo_pool[tx_slot];

                // If payload address is valid and readable, copy up to 256 bytes
                if (in_desc.dram_payload_addr != 0U && in_desc.length > 0U) {
                    const uint32_t copy_len = in_desc.length > 256U ? 256U : in_desc.length;
                    const uint8_t* const src = reinterpret_cast<const uint8_t*>(in_desc.dram_payload_addr);
                    for (uint32_t i = 0; i < copy_len; ++i) {
                        tx_payload[i] = src[i];
                    }
                }

                auto& out_desc = g_tx_ring.descriptors[tx_slot];
                out_desc.dram_payload_addr = reinterpret_cast<uint32_t>(tx_payload);
                out_desc.length = in_desc.length;
                out_desc.msg_type = in_desc.msg_type;
                out_desc.flags = in_desc.flags | 0x8000U; // Echo response flag
                out_desc.timestamp_us = hal::HardwareTimer::get_time_us();

#if defined(__riscv)
                __asm__ volatile("fence rw, rw" ::: "memory");
#endif

                g_tx_ring.head.store(tx_head + 1U, std::memory_order_release);
                hal::MsgboxDriver::ring_doorbell_to_linux(0x01U);
                echo_count++;
            }

            rx_tail++;
            g_rx_ring.tail.store(rx_tail, std::memory_order_release);
        }

        // Periodic heartbeat report into RSC_TRACE every 100 ms
        if ((echo_count % 10U) == 0U && echo_count != 0U) {
            hal::Trace::puts("[riscv] ipc_ping_test: echoed ");
            hal::Trace::put_dec("count=", echo_count);
        }

        // Non-blocking poll interval
        co_await hal::HardwareTimer::sleep_ms(1);
    }
}

extern "C" int main() {
    hal::Trace::init();
    hal::Trace::puts("[riscv] ipc_ping_test: booting and initializing clocks\n");

    if (!hal::boot::initialize_from_linux_clock_configuration(1000U)) {
        hal::Trace::puts("[riscv] ipc_ping_test: clock init failed, halting\n");
        while (true) {
#if defined(__riscv)
            __asm__ volatile("wfi");
#endif
        }
    }

    hal::MsgboxDriver::init();
    hal::Trace::puts("[riscv] ipc_ping_test: ready, starting ping responder\n");

    static auto ping_task = ipc_ping_responder_coro();
    ping_task.resume();

    while (true) {
        abstractx::IsrDispatcher::process();
#if defined(__riscv)
        __asm__ volatile("wfi");
#endif
    }

    return 0;
}
