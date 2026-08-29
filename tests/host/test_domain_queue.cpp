/*
 * Coroutine domain -> I/O (ISR) domain handoff tests.
 *
 * The HAL driver queue is an ETL SPSC queue guarded by an interrupt
 * save/restore access policy. The coroutine domain enqueues with the locked
 * push(), and the driver ISR dequeues with the unlocked pop_from_isr().
 */

#include <CppUTest/TestHarness.h>

#include <coroutine>
#include <cstdint>

#include "abstractx/domain_dispatcher.hpp"
#include "driver_request_queue.hpp"

namespace {

struct FakeRequest {
    uint32_t id{0};
    uint32_t reg{0};
};

using TestQueue = hal::DriverRequestQueue<FakeRequest, 8>;

TEST_GROUP(DriverRequestQueueGroup) {
    TestQueue queue;

    void teardown() override {
        queue.clear();
    }
};

TEST(DriverRequestQueueGroup, CoroutineDomainPushIsDrainedByIsrPop) {
    CHECK_TRUE(queue.push(FakeRequest{1u, 0x75u}));
    CHECK_TRUE(queue.push(FakeRequest{2u, 0x3Bu}));

    FakeRequest req{};
    CHECK_TRUE(queue.pop_from_isr(req));
    CHECK_EQUAL(1u, req.id);
    CHECK_EQUAL(0x75u, req.reg);

    CHECK_TRUE(queue.pop_from_isr(req));
    CHECK_EQUAL(2u, req.id);

    CHECK_FALSE(queue.pop_from_isr(req));
    CHECK_TRUE(queue.empty_from_isr());
}

TEST(DriverRequestQueueGroup, PushRejectsWhenFullInsteadOfOverwriting) {
    for (uint32_t i = 0; i < TestQueue::MAX_SIZE; ++i) {
        CHECK_TRUE(queue.push(FakeRequest{i, i}));
    }

    CHECK_FALSE(queue.push(FakeRequest{99u, 99u}));
    CHECK_EQUAL(TestQueue::MAX_SIZE, queue.size());

    FakeRequest req{};
    CHECK_TRUE(queue.pop_from_isr(req));
    CHECK_EQUAL(0u, req.id);
    CHECK_TRUE(queue.push(FakeRequest{99u, 99u}));
}

TEST(DriverRequestQueueGroup, IsrProducerFeedsCoroutineDomainConsumer) {
    CHECK_TRUE(queue.push_from_isr(FakeRequest{7u, 0x0Bu}));

    FakeRequest req{};
    CHECK_TRUE(queue.pop(req));
    CHECK_EQUAL(7u, req.id);
    CHECK_EQUAL(0x0Bu, req.reg);
}

int g_resume_count = 0;

struct CountingTask {
    struct promise_type {
        CountingTask get_return_object() { return CountingTask{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() {}
    };

    std::coroutine_handle<promise_type> handle;
};

CountingTask counting_task() {
    co_await std::suspend_always{};
    ++g_resume_count;
}

TEST_GROUP(DomainDispatcherGroup) {
    void setup() override {
        g_resume_count = 0;
        abstractx::Dispatcher::reset();
    }

    void teardown() override {
        abstractx::Dispatcher::reset();
    }
};

TEST(DomainDispatcherGroup, IsrPostResumesCoroutineInCoroutineDomain) {
    auto task = counting_task();

    CHECK_TRUE(abstractx::Dispatcher::post(task.handle));
    CHECK_TRUE(abstractx::Dispatcher::has_pending_resumes());
    CHECK_EQUAL(0, g_resume_count);

    abstractx::Dispatcher::process();

    CHECK_EQUAL(1, g_resume_count);
    CHECK_FALSE(abstractx::Dispatcher::has_pending_resumes());

    task.handle.destroy();
}

TEST(DomainDispatcherGroup, NullHandleIsRejected) {
    CHECK_FALSE(abstractx::Dispatcher::post(std::coroutine_handle<>{}));
    CHECK_FALSE(abstractx::Dispatcher::push(std::coroutine_handle<>{}));
    CHECK_FALSE(abstractx::Dispatcher::has_pending_resumes());
}

} // namespace
