/*
 * Pin routing smoke test.
 *
 * Walks every RISC-V-owned pin in turn and emits an identifying burst on it:
 * pin N in the table produces (index + 1) pulses, then holds low for the gap.
 * Probe a 40-pin header pin with a scope and count the pulses to learn which
 * SoC pin it is. Verifies the toggle path, the R_PIO/PIO bank decode, and the
 * header routing in one pass.
 */

#include <cstdint>

#include "hal_gpio.hpp"
#include "hal_trace.hpp"
#include "memory_map.h"

namespace {

struct PinUnderTest {
    uint32_t port;
    uint32_t pin;
    const char* name;
};

// Only pins this project owns. Linux-owned pins are deliberately excluded:
// PL0/PL1 (PMIC I2C), PL4/PL5 (LEDs), PL7 (vcc-3v3), PL8 (USB VBUS),
// PL11 (PCIe 3V3), PM1 (wifi-en).
constexpr PinUnderTest kPins[] = {
    {hal::Gpio::PORT_L,  2, "PL2  S_UART0_TX"},
    {hal::Gpio::PORT_L,  3, "PL3  S_UART0_RX"},
    {hal::Gpio::PORT_L,  6, "PL6  IMU_DRDY"},
    {hal::Gpio::PORT_L,  9, "PL9  FPGA_IRQ"},
    {hal::Gpio::PORT_L, 10, "PL10 DEBUG0"},
    {hal::Gpio::PORT_L, 12, "PL12 DEBUG1"},
    {hal::Gpio::PORT_L, 13, "PL13 SPARE"},
    {hal::Gpio::PORT_M,  0, "PM0  SPARE"},
    {hal::Gpio::PORT_M,  4, "PM4  S_TWI2_SCK"},
    {hal::Gpio::PORT_M,  5, "PM5  S_TWI2_SDA"},
    {hal::Gpio::PORT_J, 20, "PJ20 SPI0_CS0"},
    {hal::Gpio::PORT_J, 21, "PJ21 SPI0_CLK"},
    {hal::Gpio::PORT_J, 22, "PJ22 SPI0_MOSI"},
    {hal::Gpio::PORT_J, 23, "PJ23 SPI0_MISO"},
    {hal::Gpio::PORT_J, 24, "PJ24 SPI0_CS1"},
};

constexpr uint32_t kPinCount = sizeof(kPins) / sizeof(kPins[0]);

// Cycle-based delays: the timer/CLINT aperture is still unverified, so this
// test deliberately avoids depending on it.
void delay_cycles(uint32_t cycles) noexcept {
    for (volatile uint32_t i = 0; i < cycles; ++i) {
        __asm__ volatile("" ::: "memory");
    }
}

constexpr uint32_t kPulseHalfPeriod = 60000;  // ~200 us at 600 MHz
constexpr uint32_t kBurstGap = 3000000;       // ~10 ms low between bursts

} // namespace

extern "C" int main() {
    hal::Trace::init();
    hal::Trace::puts("[riscv] platform smoke test starting pin pulse sweep\n");

    for (uint32_t i = 0; i < kPinCount; ++i) {
        hal::Gpio::configure_pin(kPins[i].port, kPins[i].pin, hal::PinMode::OUTPUT);
        hal::Gpio::clear_pin(kPins[i].port, kPins[i].pin);
    }

    while (true) {
        for (uint32_t i = 0; i < kPinCount; ++i) {
            hal::Trace::puts("[riscv] pulsing pin: ");
            hal::Trace::puts(kPins[i].name);
            hal::Trace::puts("\n");

            const uint32_t pulses = i + 1U;
            for (uint32_t p = 0; p < pulses; ++p) {
                hal::Gpio::set_pin(kPins[i].port, kPins[i].pin);
                delay_cycles(kPulseHalfPeriod);
                hal::Gpio::clear_pin(kPins[i].port, kPins[i].pin);
                delay_cycles(kPulseHalfPeriod);
            }
            delay_cycles(kBurstGap);
        }
    }

    return 0;
}
