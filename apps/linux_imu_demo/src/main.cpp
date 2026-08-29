/*
 * Linux-side IMU IPC demo for the RISC-V IO processor architecture.
 *
 * This example models the host-side request/response path:
 *   - Linux issues an IMU read request via the IPC ring
 *   - the RISC-V IO processor performs the SPI bus transaction
 *   - the result is returned through the control plane and payload buffer
 *
 * It is intentionally lightweight and illustrative rather than production-quality.
 */

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

namespace demo {

constexpr uint32_t kRingCapacity = 16;
constexpr uint32_t kPayloadBytes = 32;

struct ImuReadRequest {
    uint32_t msg_id;
    uint32_t device_id;
    uint32_t reg_addr;
    uint32_t length;
    uint32_t payload_slot;
    uint32_t timestamp_us;
};

struct ImuReadReply {
    uint32_t msg_id;
    uint32_t device_id;
    uint32_t status;
    uint32_t length;
    uint32_t payload_slot;
    uint32_t timestamp_us;
};

struct RingState {
    std::atomic<uint32_t> head{0};
    std::atomic<uint32_t> tail{0};
    std::array<ImuReadRequest, kRingCapacity> requests{};
    std::array<ImuReadReply, kRingCapacity> replies{};
};

struct PayloadPool {
    std::array<std::array<uint8_t, kPayloadBytes>, kRingCapacity> slots{};
};

static RingState g_ring;
static PayloadPool g_payloads;

static void simulate_riscv_io_processor(const ImuReadRequest& req) {
    auto& slot = g_payloads.slots[req.payload_slot % kRingCapacity];
    std::memset(slot.data(), 0, slot.size());

    // Simulate a small IMU burst payload.
    // This models the coprocessor having completed the SPI read and placed the
    // result in a DRAM payload buffer while the SRAM ring keeps only metadata.
    slot[0] = 0x12;
    slot[1] = 0x34;
    slot[2] = 0x56;
    slot[3] = 0x78;
    slot[4] = static_cast<uint8_t>((req.reg_addr >> 0) & 0xFF);
    slot[5] = static_cast<uint8_t>((req.reg_addr >> 8) & 0xFF);
    slot[6] = 0x01;
    slot[7] = 0x02;

    ImuReadReply reply{};
    reply.msg_id = req.msg_id;
    reply.device_id = req.device_id;
    reply.status = 0;
    reply.length = req.length;
    reply.payload_slot = req.payload_slot;
    reply.timestamp_us = req.timestamp_us;

    // Simulate the ring/IPC completion handoff.
    uint32_t tail = g_ring.tail.load(std::memory_order_relaxed);
    uint32_t next = (tail + 1u) % kRingCapacity;
    g_ring.replies[tail] = reply;
    g_ring.tail.store(next, std::memory_order_release);
}

static void enqueue_request(const ImuReadRequest& req) {
    uint32_t head = g_ring.head.load(std::memory_order_relaxed);
    uint32_t next = (head + 1u) % kRingCapacity;

    if (next == g_ring.tail.load(std::memory_order_acquire)) {
        printf("IPC ring full: request dropped\n");
        return;
    }

    g_ring.requests[head] = req;
    g_ring.head.store(next, std::memory_order_release);
    printf("Linux issued IMU request: msg=%u reg=0x%08x len=%u slot=%u\n",
           req.msg_id,
           req.reg_addr,
           req.length,
           req.payload_slot);
}

static void read_reply_loop() {
    while (true) {
        uint32_t tail = g_ring.tail.load(std::memory_order_acquire);
        uint32_t head = g_ring.head.load(std::memory_order_acquire);

        if (tail == head) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        auto reply = g_ring.replies[tail];
        auto& payload = g_payloads.slots[reply.payload_slot % kRingCapacity];

        printf("Linux received IMU reply: msg=%u status=%u len=%u slot=%u bytes=%02x %02x %02x %02x\n",
               reply.msg_id,
               reply.status,
               reply.length,
               reply.payload_slot,
               payload[0],
               payload[1],
               payload[2],
               payload[3]);

        g_ring.tail.store((tail + 1u) % kRingCapacity, std::memory_order_release);
    }
}

} // namespace demo

int main() {
    printf("=== Linux IMU IPC demo ===\n");
    printf("This models the host-side request/response path to a RISC-V IO processor.\n\n");

    demo::ImuReadRequest req{};
    req.msg_id = 1;
    req.device_id = 0x69;
    req.reg_addr = 0x75;
    req.length = 8;
    req.payload_slot = 0;
    req.timestamp_us = 123456;

    demo::enqueue_request(req);

    // Simulate the RISC-V side processing the request and placing the payload into DRAM.
    std::thread io_worker([&]() {
        for (int i = 0; i < 1; ++i) {
            uint32_t head = demo::g_ring.head.load(std::memory_order_acquire);
            uint32_t tail = demo::g_ring.tail.load(std::memory_order_acquire);
            if (head != tail) {
                auto queued = demo::g_ring.requests[tail];
                demo::simulate_riscv_io_processor(queued);
                demo::g_ring.head.store((tail + 1u) % demo::kRingCapacity, std::memory_order_release);
            }
        }
    });

    demo::read_reply_loop();
    io_worker.join();

    return 0;
}
