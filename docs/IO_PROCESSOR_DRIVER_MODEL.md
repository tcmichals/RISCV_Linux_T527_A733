# IO Processor Driver Model

This note captures the intended architecture for sensor drivers and peripheral access in the T527 / A733 RISC-V IO processor project.

## 1. The core idea

The application logic is not responsible for directly driving slow peripheral buses in the hot path. Instead, the system follows a split-transaction model:

- a higher-level driver or coroutine creates a read/write request
- the IO processor loop owns the actual bus transaction
- the bus transaction happens asynchronously
- the result is returned through a ring, mailbox, or completion callback

This is the same general idea used by split-transaction I/O and AbstractX-style coroutine dispatch: slow physical I/O is hidden behind a non-blocking request/response flow.

## 2. Sensor driver pattern

For a device such as an IMU sensor (for example an MPU9250 or similar SPI device), the pattern is:

1. The driver prepares a command descriptor:
   - device select / bus instance
   - register address
   - read or write
   - payload length
   - destination buffer or DMA slot

2. The request is queued into the IO processor control plane.
   - the request metadata lives in SRAM
   - counters and descriptors remain low-latency and cache-stable

3. The IO processor loop executes the SPI transaction.
   - CS management
   - bus timing
   - command bytes
   - data bytes
   - DMA completion / timeout handling

4. The response is copied into the destination region.
   - either directly into a small in-memory result slot
   - or into a DRAM payload buffer for larger transfers

5. The waiting coroutine or driver resumes with the completed result.

This is the design pattern for an async sensor driver.

## 3. Why this matches the project architecture

The project already separates:

- fast ring metadata in SRAM
- large payload data in DRAM

That split is documented in:

- [hal/include/memory_map.h](../hal/include/memory_map.h)
- [hal/src/riscv_memory_map.ld](../hal/src/riscv_memory_map.ld)
- [apps/ipc_benchmark/src/main.cpp](../apps/ipc_benchmark/src/main.cpp)

The practical rule is:

- SRAM holds the operational state that must be low-latency and lock-free
- DRAM holds the actual bulk payloads and large transfers

## 4. The IO processor loop owns the bus

The IO processor loop is the component that should own the real bus state machine. In other words, it is responsible for:

- setting chip select
- issuing read/write commands
- waiting for completion or timeout
- handling DMA and memory buffer ownership
- posting completion to the control plane

The high-level driver should not be doing this inline in the main control flow.

## 5. Example conceptual transaction

A conceptual SPI IMU read might look like this:

- request: `read register 0x75 from sensor IMU_A`
- driver enqueues request with descriptor metadata
- IO processor picks up the request
- SPI bus transfers command and reads result
- result is stored in DRAM payload area
- completion record is written to SRAM ring metadata
- coroutine wakes and consumes the result

This is how an IMU driver should look in the architecture we are building.

## 6. Why this is the correct model

This keeps the system aligned with the goals of the project:

- coroutine-friendly control logic
- transport abstraction over physical buses
- no blocking main loop while slow bus transactions are in flight
- clear separation between metadata and payload memory

This is not a traditional monolithic firmware design with direct register reads in the main loop. It is an I/O processor design.

## 7. Summary

The sensor and peripheral drivers should behave like request producers, while the IO processor loop behaves like the transaction executor.

This is the architecture we want to keep building toward.
