# BareCTF Scheduler Tracing

## Purpose

BareCTF is the timing and CPU-accounting trace facility for the AbstractX
scheduler. It records each continuation interval as a pair of events:

- `coroutine_continue_begin`: immediately before a scheduler-owned
  `std::coroutine_handle<>::resume()` call.
- `coroutine_continue_end`: immediately after the same call returns, including
  the outcome of the continuation.

The source configuration is
[`third_party/barectf/abstractx_scheduler.yaml`](../third_party/barectf/abstractx_scheduler.yaml).
It is intentionally generic: no application, peripheral, or flight-control
specific events are required to measure scheduler CPU consumption.

## Event fields

| Field | Meaning |
| --- | --- |
| `coroutine_id` | Stable 32-bit scheduler-assigned identifier. Do not use the coroutine frame address: frame allocators can reuse it. |
| `priority` | Scheduler priority at the time of this continuation. The scheduler owns its numeric policy; lower numeric values should mean higher priority if a conventional ordering is needed. |
| `scheduler_lane` | Execution lane identifier. Use `0` for the main/bottom-half event loop; reserve other values for future worker or core lanes. |
| `wake_reason` | Begin-event reason that made the continuation runnable. |
| `outcome` | End-event result of the continuation. |

Use these initial values consistently:

| `wake_reason` | Value | `outcome` | Value |
| --- | ---: | --- | ---: |
| Initial start | 0 | Suspended | 0 |
| Timer | 1 | Completed | 1 |
| ISR/device event | 2 | Faulted | 2 |
| IPC/queue | 3 |  |  |
| Cooperative yield | 4 |  |  |
| Child continuation | 5 |  |  |

## Instrumentation rule

Every scheduler-owned resume must pass through one wrapper. The wrapper emits
`begin`, calls `handle.resume()`, then emits `end`. This includes
`IsrDispatcher::process_ready_coroutines()` and any direct resume path used by
`Event`, `Semaphore`, `AsyncQueue`, I/O completion, or coroutine combinators.

Do **not** emit BareCTF events from an ISR. An ISR only records/queues the wake
reason. The bottom-half scheduler emits the paired events when it performs the
actual resume; this keeps the writer single-context and makes the measured
interval represent continuation CPU time rather than interrupt-handler time.

Assign each task its ID and priority when it is created or registered. Keep the
mapping alive for the task lifetime. IDs must not be derived from `handle.address()`.

## CPU usage calculation

BareCTF timestamps each event using the `default` 1 GHz clock. For one matched
pair, continuation CPU time is

$$t_\mathrm{run}=t_\mathrm{end}-t_\mathrm{begin}.$$

For coroutine $c$ during a capture window $W$:

$$\mathrm{CPU\%}_c=100\frac{\sum t_\mathrm{run,c}}{W}.$$

On the current single main scheduler lane, the sum across coroutine CPU usage
should remain at or below 100%, subject to trace overhead and capture-window
boundaries. Report unmatched begin/end events as malformed or truncated trace
records rather than treating them as zero-length work.

## Generate and consume

Install BareCTF 3.1 or later on the development host. From the repository root,
generate the tracing C source/header and CTF metadata from the YAML
configuration. Keep generated output separate from the handwritten legacy
`third_party/barectf/barectf.c` until the firmware is switched to the generated
API.

The firmware trace resource is a 32 KB buffer at `0x07138000`, defined by
`IPC_TRACE_BUFFER_ADDR`, placed by `.trace_ctf_buffer` in the linker script,
and exported through the RemoteProc resource table. These three definitions
must remain identical.
Capture the resulting stream from the target's `remoteproc` debugfs trace entry
and inspect the saved output with normal text tools. This project does not
require a GUI trace viewer.

## Pigweed boundary

Use Pigweed tokenized logs for low-volume diagnostics which are not timeline
samples: queue-overflow warnings, dropped-resume counters, bad state
transitions, configuration, and error codes. Do not substitute Pigweed logs for
paired BareCTF events when calculating CPU time: logs can be filtered, dropped,
or emitted on a different path and do not preserve scheduler intervals.
