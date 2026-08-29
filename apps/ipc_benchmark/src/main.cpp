/*
 * IPC Benchmark App (AbstractX + SPSC in SRAM + Payloads in DRAM + Pigweed/BareCTF)
 */

#include "abstractx/coro.hpp"
#include "abstractx/timer.hpp"
#include "abstractx/isr_dispatcher.hpp"
#include "hal_boot.hpp"
#include "hal_timer.hpp"
#include "hal_msgbox.hpp"
#include "hal_gpio.hpp"
#include "ipc_protocol.hpp"
#include <cstdio>

// SPSC Ring mapped into SRAM C (0x07130000)
__attribute__((section(".spsc_descriptors")))
abstractx::ipc::SpscRingControl g_tx_ring;

__attribute__((section(".spsc_descriptors")))
abstractx::ipc::SpscRingControl g_rx_ring;

// Bulk DMA / Payload buffers mapped into DRAM (0x48100000)
__attribute__((section(".dram_buffers")))
uint8_t g_dram_payload_pool[128][256];

abstractx::Task<void> ipc_sender_coro() {
    uint32_t seq = 0;
    while (true) {
        uint32_t head = g_tx_ring.head.load(std::memory_order_relaxed);
        uint32_t tail = g_tx_ring.tail.load(std::memory_order_acquire);

        if ((head - tail) < abstractx::ipc::RING_SIZE) {
            uint32_t slot = head & abstractx::ipc::RING_MASK;
            uint8_t* payload = g_dram_payload_pool[slot];

            // Fill DRAM payload
            payload[0] = static_cast<uint8_t>(seq & 0xFF);
            payload[1] = static_cast<uint8_t>((seq >> 8) & 0xFF);

            // Populate SRAM descriptor
            auto& desc = g_tx_ring.descriptors[slot];
            desc.dram_payload_addr = reinterpret_cast<uint32_t>(payload);
            desc.length = 256;
            desc.msg_type = static_cast<uint16_t>(abstractx::ipc::MsgType::IMU_BURST_DATA);
            desc.timestamp_us = hal::HardwareTimer::get_time_us();

#if defined(__riscv)
            __asm__ volatile("fence rw, rw" ::: "memory");
#endif

            g_tx_ring.head.store(head + 1, std::memory_order_release);
            hal::MsgboxDriver::ring_doorbell_to_linux(0x01);
            seq++;
        }

        // Send at 1 kHz rate
        co_await hal::HardwareTimer::sleep_ms(1);
    }
}

int main() {
    if (!hal::boot::initialize_from_linux_clock_configuration(1000U)) {
        while (true) {
#if defined(__riscv)
            __asm__ volatile("wfi");
#endif
        }
    }
    hal::MsgboxDriver::init();

    printf("=== IPC Benchmark (SRAM Ring + DRAM Payloads) Started ===\n");

    static auto ipc_sender_task = ipc_sender_coro();
    ipc_sender_task.resume();

    while (true) {
        abstractx::IsrDispatcher::process();
#if defined(__riscv)
        __asm__ volatile("wfi");
#endif
    }
    return 0;
}
