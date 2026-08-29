/*
 * AbstractX Hello World & RemoteProc Trace App for T527 / A733
 * Demonstrates:
 * - Non-blocking C++20 coroutines (AbstractX)
 * - Hardware timer ticks via MTIME
 * - ISR-safe GPIO toggle for CPU measurement
 * - Debugfs trace stream output via printf
 */

#include "abstractx/coro.hpp"
#include "abstractx/timer.hpp"
#include "abstractx/isr_dispatcher.hpp"
#include "hal_boot.hpp"
#include "hal_timer.hpp"
#include "hal_gpio.hpp"
#include <cstdio>

// Coroutine 1: 1 Hz Heartbeat & Trace Log
abstractx::Task<void> heartbeat_coro() {
    uint32_t count = 0;
    while (true) {
        // Toggle pin for oscilloscope CPU timing
        hal::Gpio::toggle_pin(0 /* Port A */, 10 /* Pin 10 */);

        printf("[E907/E902] AbstractX Heartbeat #%lu (Time: %lu us)\n", 
               (unsigned long)count++, (unsigned long)hal::HardwareTimer::get_time_us());

        // Yield execution for 1000ms using AbstractX Timer Service
        co_await hal::HardwareTimer::sleep_ms(1000);
    }
}

// Coroutine 2: 100 Hz High-Frequency Sensor Task
abstractx::Task<void> sensor_loop_coro() {
    while (true) {
        // Toggle debug marker pin
        hal::Gpio::toggle_pin(0 /* Port A */, 11 /* Pin 11 */);

        // Fast 10ms non-blocking coroutine sleep
        co_await hal::HardwareTimer::sleep_ms(10);
    }
}

int main() {
    // 1. Validate Linux CCU configuration and initialize the hardware timer.
    if (!hal::boot::initialize_from_linux_clock_configuration(1000U)) {
        while (true) {
#if defined(__riscv)
            __asm__ volatile("wfi");
#endif
        }
    }

    hal::Gpio::configure_pin(0, 10, hal::PinMode::OUTPUT);
    hal::Gpio::configure_pin(0, 11, hal::PinMode::OUTPUT);

    printf("=== Allwinner T527/A733 AbstractX Coprocessor Booted ===\n");

    // 2. Retain and start stackless coroutines. Destroying a Task destroys its frame.
    static auto heartbeat_task = heartbeat_coro();
    static auto sensor_loop_task = sensor_loop_coro();
    heartbeat_task.resume();
    sensor_loop_task.resume();

    // 3. Main Lock-Free Event Loop
    while (true) {
        abstractx::IsrDispatcher::process();
#if defined(__riscv)
        __asm__ volatile("wfi");
#endif
    }

    return 0;
}
