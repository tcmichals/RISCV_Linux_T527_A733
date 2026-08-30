#include <CppUTest/TestHarness.h>

#include "hal/include/clock_config.hpp"
#include "hal/include/hal_twi.hpp"

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

TEST(ClockConfiguration, ComputesTwiClockDividersAccurately) {
    // For 24 MHz parent and 400 kHz SCL: divisor = 60 -> 10 * (2^1) * (2+1) = 60 -> N=1, M=2 (CCR = 0x0A)
    const uint32_t ccr_400k = hal::TwiDriver::compute_ccr(400000U, 24000000U);
    const uint32_t n = (ccr_400k >> 3) & 0x07U;
    const uint32_t m = ccr_400k & 0x07U;
    const uint32_t scl_actual = 24000000U / (10U * (1U << n) * (m + 1U));
    CHECK_EQUAL(400000U, scl_actual);

    // For 24 MHz parent and 100 kHz SCL: divisor = 240 -> 10 * (2^3) * (2+1) = 240 -> N=3, M=2 (CCR = 0x1A)
    const uint32_t ccr_100k = hal::TwiDriver::compute_ccr(100000U, 24000000U);
    const uint32_t n_100k = (ccr_100k >> 3) & 0x07U;
    const uint32_t m_100k = ccr_100k & 0x07U;
    const uint32_t scl_100k_actual = 24000000U / (10U * (1U << n_100k) * (m_100k + 1U));
    CHECK_EQUAL(100000U, scl_100k_actual);
}

} // namespace
