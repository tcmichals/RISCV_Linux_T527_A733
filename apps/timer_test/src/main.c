/*
 * Minimal bare-C CLINT/debugfs trace test for T527/A733 RemoteProc.
 *
 * This test intentionally avoids C++, coroutines, Linux handoff, and
 * interrupts. It validates that the RISC-V can read its CLINT time counter and
 * that Linux can observe writes to the RemoteProc trace resource as trace0.
 */

#include <stdint.h>

#include "memory_map.h"

#define TIMER_TEST_TICKS_PER_SECOND 24000000ULL

__attribute__((section(".trace_ctf_buffer")))
volatile char g_trace_buffer[128];

static uint64_t read_mtime(void)
{
    volatile const uint32_t *const mtime =
        (volatile const uint32_t *)(uintptr_t)RISCV_MTIME_ADDR;
    uint32_t high_before;
    uint32_t low;
    uint32_t high_after;

    do {
        high_before = mtime[1];
        low = mtime[0];
        high_after = mtime[1];
    } while (high_before != high_after);

    return ((uint64_t)high_before << 32) | low;
}

static void write_trace(const char *message)
{
    uint32_t index = 0;
    while (message[index] != '\0' && index + 1U < sizeof(g_trace_buffer)) {
        g_trace_buffer[index] = message[index];
        ++index;
    }
    g_trace_buffer[index] = '\0';
}

void riscv_trap_handler(void)
{
    write_trace("timer_test: FAIL unexpected trap\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

int main(void)
{
    uint64_t last_heartbeat = read_mtime();
    uint32_t heartbeat = 0;

    write_trace("timer_test: started; polling CLINT mtime\n");
    for (;;) {
        const uint64_t now = read_mtime();
        if (now - last_heartbeat < TIMER_TEST_TICKS_PER_SECOND) {
            continue;
        }

        last_heartbeat += TIMER_TEST_TICKS_PER_SECOND;
        if ((heartbeat++ & 1U) == 0U) {
            write_trace("timer_test: alive A; CLINT mtime advancing\n");
        } else {
            write_trace("timer_test: alive B; CLINT mtime advancing\n");
        }
    }
}
