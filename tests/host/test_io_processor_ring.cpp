#include <CppUTest/TestHarness.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace {

struct Message {
    uint32_t id;
    uint32_t reg;
    uint32_t len;
};

struct Ring {
    std::atomic<uint32_t> head{0};
    std::atomic<uint32_t> tail{0};
    std::array<Message, 8> queue{};
};

TEST_GROUP(IoProcessorRing) {
    Ring ring;

    void setup() override {
        ring.head.store(0, std::memory_order_relaxed);
        ring.tail.store(0, std::memory_order_relaxed);
        ring.queue.fill(Message{0, 0, 0});
    }
};

TEST(IoProcessorRing, RingAcceptsMessages) {
    Message msg{1, 0x75, 8};
    uint32_t head = ring.head.load(std::memory_order_relaxed);
    ring.queue[head] = msg;
    ring.head.store((head + 1u) % ring.queue.size(), std::memory_order_release);

    CHECK_EQUAL(1u, ring.head.load(std::memory_order_acquire));
    CHECK_EQUAL(1u, ring.queue[0].id);
    CHECK_EQUAL(0x75u, ring.queue[0].reg);
    CHECK_EQUAL(8u, ring.queue[0].len);
}

TEST(IoProcessorRing, RingDetectsWrap) {
    for (uint32_t i = 0; i < ring.queue.size(); ++i) {
        uint32_t head = ring.head.load(std::memory_order_relaxed);
        ring.queue[head] = Message{i, i, i};
        ring.head.store((head + 1u) % ring.queue.size(), std::memory_order_release);
    }

    CHECK_EQUAL(0u, ring.head.load(std::memory_order_acquire));
    CHECK_EQUAL(7u, ring.queue[7].id);
}

} // namespace
