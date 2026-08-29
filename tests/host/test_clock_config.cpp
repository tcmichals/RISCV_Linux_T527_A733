#include <CppUTest/TestHarness.h>

#include "hal/include/clock_config.hpp"

namespace {

hal::clocking::ClockConfiguration valid_configuration() {
    hal::clocking::ClockConfiguration configuration{};
    configuration.magic = hal::clocking::kClockConfigMagic;
    configuration.abi_version = hal::clocking::kClockConfigAbiVersion;
    configuration.size_bytes = sizeof(configuration);
    configuration.generation = 1;
    configuration.riscv_core_hz = 600000000U;
    configuration.timer_counter_hz = 24000000U;
    configuration.uart_parent_hz = 24000000U;
    configuration.spi0_parent_hz = 200000000U;
    configuration.spi1_parent_hz = 200000000U;
    return configuration;
}

TEST_GROUP(ClockConfiguration) {
};

TEST(ClockConfiguration, AcceptsCompleteCurrentAbi) {
    const auto configuration = valid_configuration();

    CHECK_TRUE(configuration.is_valid());
    CHECK_EQUAL(64U, sizeof(configuration));
}

TEST(ClockConfiguration, RejectsIncompleteOrIncompatibleValues) {
    auto configuration = valid_configuration();
    configuration.timer_counter_hz = 0;
    CHECK_FALSE(configuration.is_valid());

    configuration = valid_configuration();
    configuration.abi_version++;
    CHECK_FALSE(configuration.is_valid());
}

} // namespace
