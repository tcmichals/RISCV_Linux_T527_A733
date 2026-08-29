#pragma once

/*
 * HAL driver request queue: the coroutine domain -> I/O (ISR) domain handoff.
 *
 *   coroutine domain producer : push() / emplace()      (interrupts masked)
 *   driver ISR consumer       : pop_from_isr()          (already masked)
 *
 * This is `etl::queue_spsc_isr` with the AbstractX interrupt save/restore
 * access policy; no hand-rolled ring is maintained here.
 */

#include <cstddef>

#include <etl/memory_model.h>
#include <etl/queue_spsc_isr.h>

#include "abstractx/interrupt_lock.hpp"

namespace hal {

// ETL TAccess policy used by every HAL driver queue.
using IrqLock = abstractx::InterruptLock;
using IrqGuard = abstractx::InterruptGuard;

template <typename T, size_t Capacity>
using DriverRequestQueue = etl::queue_spsc_isr<T,
                                               Capacity,
                                               IrqLock,
                                               etl::memory_model::MEMORY_MODEL_SMALL>;

} // namespace hal
