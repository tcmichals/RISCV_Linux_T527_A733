#pragma once
#include <cstdint>
#include <atomic>

namespace abstractx::ipc {

constexpr uint32_t IPC_MAGIC = 0x544C5049; // 'TLPI'
constexpr size_t RING_SIZE = 128;          // Power of 2
constexpr size_t RING_MASK = RING_SIZE - 1;

enum class MsgType : uint16_t {
    HEARTBEAT       = 0x0001,
    PIGWEED_LOG     = 0x0010,
    BARECTF_TRACE   = 0x0020,
    PCIE_TLP_STREAM = 0x0030,
    IMU_BURST_DATA  = 0x0040
};

// 16-byte zero-copy descriptor placed in SRAM C (0x07130000)
struct alignas(16) IpcDescriptor {
    uint32_t dram_payload_addr; // Physical 32-bit address in DRAM
    uint32_t length;            // Payload length in bytes
    uint16_t msg_type;          // MsgType
    uint16_t flags;             // Flags / Channel ID
    uint32_t timestamp_us;      // Timestamp from E907 hw timer
};

// SPSC Ring Control Header in SRAM C
struct alignas(64) SpscRingControl {
    std::atomic<uint32_t> head{0}; // Producer index
    uint8_t pad0[60];              // Cache line isolation
    std::atomic<uint32_t> tail{0}; // Consumer index
    uint8_t pad1[60];
    IpcDescriptor descriptors[RING_SIZE];
};

} // namespace abstractx::ipc
