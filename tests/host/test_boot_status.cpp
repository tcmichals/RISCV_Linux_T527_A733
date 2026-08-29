#include <CppUTest/TestHarness.h>

#include "hal/include/hal_boot.hpp"

namespace {

hal::boot::Status valid_status() {
    hal::boot::Status status{};
    status.magic = hal::boot::kBootStatusMagic;
    status.abi_version = hal::boot::kBootStatusAbiVersion;
    status.size_bytes = sizeof(status);
    status.generation = 1;
    status.state = static_cast<uint32_t>(hal::boot::State::Ready);
    status.riscv_core_hz = 600000000U;
    return status;
}

TEST_GROUP(BootStatus) {
};

TEST(BootStatus, AcceptsACompleteReadyRecord) {
    const auto status = valid_status();

    CHECK_TRUE(status.is_valid());
    CHECK_EQUAL(64U, sizeof(status));
}

TEST(BootStatus, RejectsUnknownStateAndIncompleteRecord) {
    auto status = valid_status();
    status.state = 5U;
    CHECK_FALSE(status.is_valid());

    status = valid_status();
    status.generation = 0U;
    CHECK_FALSE(status.is_valid());
}

TEST(BootStatus, BuildsReadyStatusWithClockConfigurationIdentity) {
    const auto status = hal::boot::make_status(
        hal::boot::State::Ready,
        hal::boot::FailureReason::None,
        7U,
        6U,
        480000000U);

    CHECK_TRUE(status.is_valid());
    CHECK_EQUAL(static_cast<uint32_t>(hal::boot::State::Ready), status.state);
    CHECK_EQUAL(7U, status.generation);
    CHECK_EQUAL(6U, status.clock_configuration_generation);
    CHECK_EQUAL(480000000U, status.riscv_core_hz);
}

} // namespace
