# UART DMA RX / Character Timeout (RTO) Explained

This note documents how the Allwinner T527 / A733 coprocessor can implement a practical serial RX path without a full custom DMA engine for every byte stream.

## 1. The key fact

The hardware UART does not require a separate, bespoke DMA RX engine to receive a packet stream efficiently. The UART already exposes the behavior needed for a low-overhead, interrupt-driven RX path:

- RX FIFO holds incoming bytes
- `Data Ready` interrupt fires when FIFO has data
- `Character Timeout` interrupt fires when the line has been idle for the programmed timeout while bytes remain unread

This is the UART `RTO` / receiver timeout interrupt path, often described as `IIR ID = 0b1100`.

That means the firmware can treat a burst as “complete” when one of these conditions occurs:

1. the FIFO reaches a threshold, or
2. the line has been idle long enough and unread bytes remain buffered

This is exactly the signal we want for packet framing and DMA transfer completion.

## 2. Why this matters for DMA-backed serial RX

For a coprocessor that wants to receive a UART stream into DRAM without polling every byte, the normal approach is:

- let the UART fill a ring buffer or FIFO in hardware
- on `RX Data Ready` or `Character Timeout`, copy the available bytes into a software buffer or DMA target
- trigger a software or DMA transfer once the burst is complete
- keep the control path, counters, and ring state in fast SRAM
- keep large payloads in DRAM

This gives a clean split:

- SRAM: ring headers, counters, doorbells, descriptor metadata
- DRAM: actual payload pages and bulk receive buffers

That matches the architecture goal: keep latency-sensitive control in SRAM and large data in DRAM.

## 3. How the firmware uses this in practice

The project driver in `hal/include/hal_uart_dma.hpp` does the following:

1. Initializes UART FIFO and enables the receive interrupt sources.
2. Watches the UART `IIR` register for interrupt IDs:
   - `0x04`: RX data available
   - `0x0C`: Character Timeout / RTO
3. Reads all currently available bytes from the FIFO into a target span.
4. Posts the awaiting coroutine back to the dispatcher.

In short, the RX completion event is not “DMA finished” in the strict hardware-driver sense; it is “UART has enough data or has been idle long enough, so the buffer is ready to consume.”

That is enough to build a sane serial RX pipeline.

## 4. Why this is a good fit for this coprocessor design

This approach provides several benefits:

- low CPU overhead: no byte-by-byte polling
- good burst handling: packets naturally flush on idle gap
- predictable software framing: use the timeout as a packet boundary
- simple memory split: SPSC counters/descriptors remain in SRAM; payloads live in DRAM

This is exactly the pattern we want for a lightweight remote-proc / mailbox / DMA payload design.

## 5. Recommended design pattern

For the T527 / A733 coprocessor firmware, the recommended structure is:

- `SRAM_SHARED`:
  - ring control words
  - producer/consumer counters
  - mailbox doorbells
  - small descriptor metadata
- `DRAM_BUF`:
  - large UART receive buffers
  - DMA payload pools
  - trace / logging storage
- UART ISR:
  - copies FIFO content into a buffer
  - marks data ready when threshold or timeout fires
- higher layer:
  - parses the framed stream
  - copies or forwards the payload
  - updates SPSC state

This keeps the high-frequency coordination path in fast SRAM while allowing bulk data to live in DRAM without cache incoherence problems.

## 6. Important clarification

This is not a claim that the chip has a custom DMA RX engine that magically receives serial bytes into DRAM without CPU involvement.

The correct interpretation is:

- UART hardware provides a FIFO plus timeout interrupt
- firmware uses this to snapshot received bytes into RAM efficiently
- the result is effectively “DMA-assisted serial RX” in the software architecture sense, even if the underlying mechanism is FIFO + interrupt + software buffer handoff

## 7. Source references in this repo

- [engineering_log.md](../engineering_log.md#L18-L29)
- [hal/include/hal_uart_dma.hpp](../hal/include/hal_uart_dma.hpp#L23-L36)
- [hal/include/hal_uart_dma.hpp](../hal/include/hal_uart_dma.hpp#L67-L86)

These are the core evidence points behind the design.
