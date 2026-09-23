# Remote Stepping over Serial — Design Notes

Status: exploratory. Not scheduled for implementation. This document captures the
architecture discussed for offloading step-pulse generation from the main FluidNC
controller to a remote MCU connected over a 1 Mbaud UART, reusing the existing
IO-expander/pendant wire and firmware lineage (Airedale).

## 1. Goal

Split CNC step generation across a serial link so that a remote MCU generates step
pulses while FluidNC continues to own G-code interpretation, planning, and velocity
profiling unchanged.

## 2. Split point

Cut between `Stepper::prep_buffer()` and `Stepper::pulse_func()`, i.e. at the
existing `segment_t` / `st_block_t` boundary (`FluidNC/src/Stepper.cpp`):

- `prep_buffer()` (planner-facing, runs on the cooperative main task) stays
  entirely unchanged. It keeps computing acceleration ramps and emitting
  `segment_t` records into `segment_buffer`, exactly as today.
- `pulse_func()` (ISR-facing, per-Bresenham-tick) is what gets replaced/bypassed
  for this configuration. Its logic — Bresenham stepping, step/dir pin output,
  position-counter increment — gets reimplemented on the remote MCU instead.

This is *not* implemented as a new `Driver/step_engine.h` backend. That interface
is invoked once per Bresenham tick (potentially kHz rate) from inside
`pulse_func()`/`Stepping::step()` — the wrong granularity for a serial link. A
`step_engine_t`-shaped remote backend would still run full Bresenham math locally
on the ESP32 and only remote the last-mile pin toggling, reproducing the exact
per-tick bandwidth problem this design avoids. The real boundary is one layer up:
a new consumer that drains `segment_buffer` directly, in place of the whole
ISR + `step_engine_t` stack.

Concurrency model already present in FluidNC, relevant to where new code goes:

- G-code execution (fills `block_buffer` via `plan_buffer_line()`) and
  `prep_buffer()` (drains `block_buffer` into `segment_buffer`) are **not**
  independent concurrent flows. Both run on the same cooperative task —
  `execute_line()` runs inline inside `protocol_main_loop()`, and
  `mc_line()`'s full-buffer spin-wait calls `protocol_execute_realtime()`, which
  calls `Stepper::prep_buffer()`. Only `pulse_func()` (the ISR) is a genuinely
  separate concurrency domain.
- `segment_buffer` is the ultimate gating resource today: `prep_buffer()`'s outer
  loop condition (`while (segment_buffer_tail != segment_next_head)`) is checked
  before anything else, so a full `segment_buffer` transitively blocks
  `block_buffer` draining and therefore new G-code ingestion. In the offload
  design, remote credit inherits this same role.

## 3. What crosses the wire

Two message classes, mirroring the existing `segment_t` (frequent) /
`st_block_t` (infrequent, only on block-index change) split:

- **Block data** — `steps[axis]`, `step_event_count`, `direction_bits`,
  `is_pwm_rate_adjusted` — sent only when a new `plan_block_t` is loaded.
- **Segment data** — `n_step`, `isrPeriod` (sent as an abstracted time unit, not
  raw ESP32 timer ticks — the remote has different timer hardware), spindle/laser
  duty — sent at the segment cadence.

Segment cadence is capped at ~100/sec by `DT_SEGMENT` (`1/ACCELERATION_TICKS_PER_SECOND`,
10 ms) regardless of step rate — AMASS-style tick/period scaling means higher step
rates change the *values* in fixed-size fields, not the message rate or size.

## 4. Bandwidth

Rough estimate at steady state: ~1.2 KB/s for the segment stream (100/sec ×
~12 bytes), well under 1 KB/s typical for block messages, ~3 KB/s for a periodic
reverse-channel position/credit report — a few KB/s total against a 1 Mbaud link's
~100 KB/s effective payload capacity. Not the binding constraint; multiplexing and
scheduling are (see §9, §11).

Pathological cases:

- **Fine arc interpolation at high feed**: `mc_arc()` pre-chops arcs into many
  tiny linear blocks based on chordal tolerance, independent of feed rate. At high
  feed rate these can complete in far less than 10 ms, forcing more (smaller)
  segment/block messages. Self-limiting in practice — FluidNC's own CPU-side ramp
  math in `prep_buffer()` becomes the bottleneck before the wire does, and it's a
  bounded, per-feature cost (a single contour), not sustained over a whole job.
- **Laser raster**: many short (~0.1 mm) moves, each its own `plan_block_t`, each
  completing in far under 10 ms — so block rate ≈ segment rate ≈ pixel rate, which
  can be 1000–5000/sec for realistic raster speeds. Unlike the arc case, this is
  *sustained* over an entire image, and it's the case that actually motivates the
  compression scheme in §7.

## 5. Flow control: push against credit, queuing on both ends

Today: `pulse_func()` **pulls** from `segment_buffer`, paced by real time.

Proposed: whatever replaces `pulse_func()`'s role on the remote **pulls** from its
own local receive queue exactly the same way; FluidNC's side **pushes** completed
segments onto the wire as fast as credit allows. Effectively, `segment_buffer` is
pushed over the wire, with a local copy queuing on each side:

- FluidNC-side `segment_buffer` becomes a **staging buffer pending transmission**
  rather than the system's entire real-time cushion.
- The remote's own receive queue becomes the **new real-time cushion** — it needs
  to be sized more generously than the current ~120 ms (`_segments` × `DT_SEGMENT`),
  since it now also has to absorb serial/link jitter, not just cooperative-task
  scheduling jitter.
- Credit flows back asynchronously (not a per-segment request/response), mirroring
  the FIFO-topping-up pattern already used by `rp2040/pio_engine_delayed.cpp`:
  push whenever there's room, treat underrun as an observable fact reported back,
  not a missed deadline.
- Whatever gates delivery of this credit signal becomes the new gating factor for
  the *entire* producer chain, per §2 — link latency here throttles G-code
  ingestion, not just motion smoothness.

### Where the send call lives

At the very top of `prep_buffer()`, unconditional, before any of its existing
early-return paths: *if there are queued segments, try to send them.* This is
better than trying to call it at the one "correct" point inside `prep_buffer()`'s
loop (which has several early returns, including one right after a segment is
finalized during a forced-termination bail) — placing it first means it runs on
every call regardless of exit path, picks up anything left over from the previous
call within roughly one loop iteration, and also handles credit reopening with no
new segment produced, for free. No separate `Module`/poller needed; it's an
ordinary function call inside code already running on the same cooperative task
at the same high cadence. The call itself must be non-blocking (check credit,
push what fits, return) since `prep_buffer()` is invoked from real-time-sensitive
paths (parking, cycle start) that can't tolerate stalling on a wedged link.

## 6. Remote-side architecture

- Duplicates the *logic* of `pulse_func()` (Bresenham stepping from received
  block/segment data), not its interface. No `step_engine_t` abstraction needed:
  that exists to support several genuinely different physical peripherals across
  FluidNC's ESP32/RP2040 board family; a purpose-built remote is one fixed chip
  with one fixed pin mapping, decided at hardware design time. Direct GPIO
  manipulation — the style of pre-abstraction FluidNC/Grbl `pulse_func()` code —
  is the right level of complexity here, and also removes a vtable dispatch from
  the now-timing-critical inner loop on a slower MCU.
- **Drop AMASS.** It exists to stretch tick rate on a constrained ISR — more
  precisely, it (and the 16-bit field widths still visible in `segment_t` today)
  trace back to Grbl's AVR heritage (8-bit MCU, 16-bit hardware timers), not to
  anything ESP32-specific. New remote firmware has no Grbl/AVR lineage to
  preserve and can size fields/timers for what it actually needs.
- Must reimplement the position-counter side effect of `Stepping::step()`
  (`axis_steps[axis] += increment`) independently, since that's what drives
  `get_steps()`/`get_mpos()`/status reports on the FluidNC side and needs to be
  fed from the reverse-channel step reports instead.
- Its last-mile pulse generation doesn't need to be a strict period-locked timer
  ISR — it can reuse the same "pull as needed" idiom already present in this
  codebase (`rp2040/pio_engine_delayed.cpp`: a hardware FIFO/DMA channel drained
  opportunistically, with underrun as the observable failure mode), applied one
  level lower than the FluidNC-side segment stream. Same discipline recursively
  at two boundaries: FluidNC segment buffer → UART (coarse, ~100 Hz), remote's
  local tick queue → physical pins (fine-grained).
- **Last-mile output: direct GPIO vs. SPI-fed driver chain.** Direct GPIO
  toggling (§6 default) is simplest, but an SPI + DMA shift-out to a chain of
  serial-fed constant-current LED-driver chips is a real alternative — the same
  trick FluidNC already uses via I2S on ESP32 (`esp32/esp32/i2s_engine.cpp`,
  `esp32/esp32s3/i2s_engine_dedicated.cpp`) to multiply GPIO through a
  shift-register chain, just via SPI instead of a repurposed I2S peripheral.
  Compelling reason: LED-driver chips bring built-in overcurrent/short-circuit
  protection that a plain 74AHCT595 output stage doesn't have — a real,
  recurring field-failure mode on exposed screw-terminal boards (a dead short
  destroys a plain shift register's output stage outright). Bonus: the whole
  chain latches atomically on one clock edge, giving synchronized multi-axis
  step/dir updates by construction (no risk of one axis's write landing a few
  cycles before another's, unlike direct-port toggling when signals span more
  than one GPIO port's atomic set/reset register) — and DMA-driven SPI moves
  the pin-update work off the CPU's critical path, complementary to using a
  cheaper/slower part. Two things to verify per chosen chip before committing,
  not assumed: (1) a constant-current LED-driver output is a great match for a
  downstream opto-isolated STEP/DIR input (arguably better-suited than the
  usual resistor-limited logic drive), but needs checking against any target
  that instead expects a direct CMOS logic-level input; (2) edge speed/pulse
  timing against the chip's datasheet — almost certainly fine given LED PWM
  refresh rates are typically far above stepper pulse rates, but a check, not
  an assumption.

  **Frame/latch clock**: unlike I2S (which provides a hardware word-select
  line the ESP32 engines already exploit for this purpose), plain SPI has no
  native per-frame framing signal — the chain's LATCH/LE/STCP pin needs to be
  driven separately. A software GPIO toggle from the SPI/DMA transfer-complete
  interrupt would have been tight enough for what this design needs — the
  driving motivation for the whole SPI-chain approach is the shorted-output
  failure mode, not timing precision. But since chip selection (§12) landed on
  STM32G0 for pin-count/GPIO reasons and G0 has NSSP (`SPI_CR2`, confirmed —
  F103 doesn't), use it: it's a strictly lower-jitter path than a software
  toggle (the pulse is generated entirely within the SPI peripheral's hardware
  state machine, no interrupt-latency term at all) and costs nothing extra
  given the chip already has it. No need for a fully timer-synchronized
  transfer+latch scheme beyond that.

  **Pulse duration and dir delay, settable at startup, timed by sample
  count.** Both must arrive at the remote as configuration (not compiled in),
  mirroring `step_engine_t::init(dir_delay_us, pulse_us, ...)` on the FluidNC
  side. Pulse *duration* specifically should not be a separate timer/compare
  mechanism — FluidNC already has a working reference implementation of the
  right technique, the I2S stepping engine (`esp32/esp32/i2s_engine.cpp`): a
  fixed-rate sample clock (`i2s_frame_us`, 1-4µs) drives the output FIFO, and
  pulse width is just a *count* of consecutive fixed-period samples carrying
  the asserted pattern before reverting to baseline
  (`_pulse_counts = ceil(pulse_us / frame_us)`,
  [i2s_engine.cpp:441](../FluidNC/esp32/esp32/i2s_engine.cpp)) — no separate
  timer channel needed for pulse width at all. The SPI/NSSP version is the
  same architecture with a timer-triggered DMA transfer standing in for the
  I2S FIFO's fixed output rate: a small circular buffer of frame-slots
  (mirroring `FIFO_THRESHOLD`/`FIFO_RELOAD`'s role — a handful of slots, tens
  of bytes, refilled on a DMA half/transfer-complete interrupt), each frame
  boundary latched via NSSP, with the real Bresenham tick logic deciding how
  many "pulse" vs. "baseline" frames to enqueue. Dir delay can start with the
  same simple busy-wait `finish_dir()` uses today (direction changes are
  infrequent enough that this hasn't been worth optimizing on ESP32 either),
  with the same sample-count technique available later if it turns out to
  matter. This supersedes the single-double-buffered-transfer picture
  suggested earlier for the DMA buffer — it's a small circular buffer of
  fixed-rate frames, not one-shot transfers per Bresenham tick, though still
  a rounding error against the RAM budget in §12.

## 7. Real-time inputs: colocate probe/limits with the remote

Today, probe response is not ISR-synchronous even on the single-board design:
GPIO ISR → `protocol_send_event()` → `event_queue` → serviced whenever
`protocol_main_loop()` next runs → `protocol_do_probe()` reads `get_steps()` and
either hard-stops or decelerates. At least one RTOS task-scheduling hop of
latency exists already.

If the remote MCU also owns the probe/limit inputs, it can check them from
inside its own step-generation loop — the tightest possible context — collapsing
response to at most one step period, with no queue hop:

- `probe_hard_stop` becomes a fully local decision on the remote (zero round-trip).
- The default decel-stop path still needs FluidNC in the loop (ramp recompute is
  planner-side work, like feed hold), but the position data feeding that recompute
  is now exact — the remote reports back precisely which step of which segment it
  stopped at, rather than a value already lagged by event-queue latency.

This is a genuine latency *improvement* over the current single-board design, not
merely "no worse than a round trip."

## 8. Reverse channel

- Periodic status: segments-consumed counter (monotonic, credit derived by
  comparison — tolerant of a dropped report), underrun counter (mirrors
  `pio_underrun_count`), absolute per-axis step position (for resync).
- Urgent, out-of-band: probe/limit/abort partial-completion report — reason code,
  which block generation, exact steps executed (and, with segment repeat-count
  runs in play, how many repeats completed plus partial count into the one in
  progress) — needed for exact position reconciliation.

## 9. Wire framing

Follows the existing reserved-byte convention already used by the Channel I/O
protocol (`Expander_ACK`=0xB2, `NAK`=0xB3, `RST`=0xB4) rather than the UTF-8
codepoint packing used for pin get/set — that packing exists to keep small
values printable-ish, which buys nothing for a pure binary payload. Four new
reserved lead bytes:

| Byte | Name | Direction | Payload |
|---|---|---|---|
| 0xB5 | `SEG_BLOCK` | FluidNC→remote | Bresenham block parameters |
| 0xB6 | `SEG_SEGMENT` | FluidNC→remote | one ~10ms segment (or a repeated run) |
| 0xB7 | `SEG_STATUS` | remote→FluidNC | periodic credit/position/health |
| 0xB8 | `SEG_STOP` | remote→FluidNC | urgent partial-completion report |

Axis count fixed by handshake, not per-message — payloads are fixed-size, no
length byte needed.

**`SEG_BLOCK`**: generation (1B, wrapping — segments reference it) · direction
bits + flags (1B) · `steps[axis]` and `step_event_count` sized to match the
existing internal type (`steps_t` is `int32_t`; `st_block_t::steps[]`/
`step_event_count` are `uint32_t`) — 4 bytes each is not overkill here: a single
line can span a large fraction of machine travel (hundreds of thousands of steps
is unremarkable; multi-revolution rotary moves push into the millions) — matches
what the rest of the system already uses for this exact quantity, at zero
marginal bandwidth cost since this message class is inherently rare *except* in
the raster case (see §10). · CRC.

**`SEG_SEGMENT`**: generation (1B) · `n_step` (2B — sufficient without AMASS;
realistic step rates over 10ms don't approach 65535) · period (3B, fixed absolute
time unit, not raw remote timer ticks) · spindle/laser duty (2B, reusing the
existing 0–1000 / 0.1%-resolution convention from the Channel I/O SET sequence
spec) · **repeat count** (2B, see §10) · CRC.

**`SEG_STATUS`**: segments_consumed (2B, monotonic) · underrun_count (2B,
monotonic) · `axis_steps[axis]` (4B each, absolute) · CRC.

**`SEG_STOP`**: reason (1B) · generation (1B) · repeats_completed (2B) ·
steps_into_current_repeat (2B) · CRC.

Open, not yet decided: CRC width (8 vs 16 bit), whether a rolling frame counter
is needed in addition to `generation` for loss detection, resync policy on a bad
frame.

## 10. Compression for the sustained-high-rate case (laser raster)

Two small, general, composable mechanisms — not a dedicated "raster mode":

1. **`SEG_BLOCK` dedup.** Before serializing a new block, compare its
   `steps[]`/`step_event_count`/`direction_bits` (exact integer comparison, not
   fuzzy — already-rounded by the time they reach `st_block_t`) against the
   currently-loaded block. If identical, skip transmission entirely and keep
   emitting `SEG_SEGMENT` against the same `generation`. Needs zero new
   receiver-side logic — "many segments referencing one generation" is already
   required for any block that spans multiple real segments.
2. **`SEG_SEGMENT` repeat count.** A field on the existing message (not a
   separate `SEG_SEGMENT_RUN` type) meaning "execute this `(n_step, period,
   power)` tuple N times," defaulting to 1. Natural because `SEG_SEGMENT` already
   carries no geometry — a run of identical raster pixels *is* a run of identical
   `(n_step, period, power)` tuples under one unchanged generation.

Gated on `is_pwm_rate_adjusted` (laser mode) — cheap, already-computed signal.
The reasoning is *not* "ordinary segments never repeat" (constant-feed cruise
segments in ordinary milling repeat just as reliably, often more so, since
spindle speed rarely changes mid-line while laser power changes on purpose,
frequently). It's that ordinary milling's segment rate was never a bandwidth or
latency problem to begin with (§4), so there's no reason to pay even the small
delay this needs (below) in a regime that doesn't need it.

The gate correctly excludes the other identified high-rate case, fine arc
interpolation, but not merely because that case is lower-stakes — it structurally
has nothing to compress. `mc_arc()` computes a fresh `linear_per_segment[axis]`
per chord, so geometry rotates continuously; there is no repeat to find there
regardless of gating.

**Latency cost of run detection**: to know whether segment *N* can fold into a
run with *N+1*, the sender must see *N+1* before committing *N* — a bounded,
one-loop-iteration hold-back in the worst case (eager sender, nothing yet to
compare), often avoided entirely since a streaming raster job usually keeps
`block_buffer` far enough ahead that several segments are already available to
scan when a send is attempted. This is an ordinary Nagle-style write-coalescing
tradeoff and needs the same discipline to stay bounded: unconditional flush
triggers (end of motion/segment stream, feed hold/abort, local buffer nearing
capacity, or simply "no new segment materialized this call") so a held-back
segment is never withheld indefinitely.

Side effects to implement correctly, not open questions: `SEG_STATUS`'s
`segments_consumed` must count actual executed segments (increment by the
repeat count as each repetition completes), not messages received; `SEG_STOP`
needs the two-value (repeats-completed, partial-count) form described in §9 to
reconcile position mid-run.

## 11. UART contention / prioritization

The daisy-chained topology (FluidNC ↔ IO expander ↔ pendant, per the shipping
Airedale board) shares one physical wire per hop between this new protocol and
existing IO-expander/pendant traffic. Distinguishing message types (§9) only
solves *parsing*; it does not solve *scheduling* — once bytes are handed to a
UART driver's TX buffer they transmit strictly in FIFO order with no way to
reorder afterward, so a large lower-priority write queued first will delay a
higher-priority one queued after it regardless of framing.

- **Expander side**: it already does byte-level forwarding by design ("forward
  everything it doesn't handle locally" — `config/uart_sections.md`). Needs two
  logically separate output queues multiplexed onto the one physical UART, with
  segment-protocol traffic strictly prioritized over forwarded pendant bytes —
  and forwarded data must be drip-fed in small chunks (not handed to the UART as
  one large atomic write) so the priority check is re-evaluated between chunks.
  Symmetric in the upstream direction (expander's own generated traffic over
  bytes it's relaying from the pendant).
- **FluidNC side**: the same problem exists in reverse and is easy to miss since
  it looks solved once the expander side is fixed. `UartChannel::write()`
  already demonstrates the needed chunking pattern (80-byte pieces, on the
  `_addCR` branch, `FluidNC/src/UartChannel.cpp`) — but a channel carrying this
  protocol must run `_addCR = false` (CR-injection would corrupt a binary
  payload), which takes the single unchunked `_uart->write(buffer, length)` path
  instead, with no interleaving opportunity. Fix: chunk that branch too, for a
  channel configured for this protocol, checking a pending-high-priority-frame
  flag between chunks.
  - Practical scope is narrow: mainly `report_realtime_status()`, which builds
    and writes a single `LogStream` in one shot, periodically
    (`report_interval_ms`). Ordinary traffic on this channel (`ok` acks,
    `EXP`/`SET` sequences) is already small by design. A single ~100–150 byte
    status line costs ~1–1.5 ms at 1 Mbaud on its own — bounded and probably
    tolerable — but several bulky writes landing back-to-back (an auto-report
    coinciding with an on-demand `?` reply) could stack past whatever margin the
    remote's receive buffer provides; active interleaving bounds this, passive
    buffer sizing only bounds it probabilistically.

## 12. Hardware

The pin math below changed materially once SPI-shift-chain output (§6) entered
the picture: step/dir for an arbitrary axis count collapses to ~3 MCU pins
(SCK, MOSI, NSS-as-latch) instead of ~2 pins per axis, so the earlier
"need a 64-pin package" conclusion (driven purely by one-GPIO-per-signal
step/dir wiring) no longer holds. This makes NSS pulse mode (`SPI_CR2` NSSP)
directly relevant to chip choice, not just a nice-to-have: it's what lets the
chain's latch ride the SPI peripheral's own NSS pin in hardware instead of
costing a dedicated GPIO plus a software toggle.

**F103 has hardware-managed NSS, but not the pulse variant this needs.** With
`SSM=0`/`SSOE=1` in master mode, F103's SPI drives NSS low as soon as the
peripheral is enabled (`SPE=1`) and holds it low continuously until `SPE=0`
(RM0008) — a level tied to peripheral-enable state, not a pulse between
frames. `SPI_CR2` doesn't define an `NSSP` bit at all (only `RXDMAEN`,
`TXDMAEN`, `SSOE`, `FRF`, `ERRIE`, `RXNEIE`, `TXEIE`) — that bit, and the
behavior this design actually needs (a brief low pulse *between consecutive
frames while `SPE` stays continuously enabled*, i.e. a latch after every
frame with no peripheral reconfiguration), belongs to the newer "SPI v2"
peripheral ST introduced starting with F0/F3/L0/L4/G0. Getting a per-frame
latch out of F103's hardware NSS would mean toggling `SPE` off and on around
every single transfer — real software involvement each frame, arguably no
better than a plain GPIO toggle. **STM32G0 has `NSSP`** (`SPI_CR2`, RM0454) —
this is what argues for G0 over continuing with the Airedale-proven F103
family, despite losing toolchain continuity.

Pin budget for a 48-pin G0 with SPI/NSSP output, `STM32G030C8T6` (44 GPIO
nominal): SPI (3) + 2× USART (4) + SWD (2) + crystal (2) = 11 fixed, leaving
**~33 GPIO free** — comfortably past the ~32 target, in the 48-pin package,
with real margin for probe/limit/e-stop inputs and spares. (The shift-chain
architecture's axis-count independence isn't NSSP-specific — F103 could adopt
the same scheme with a software-toggled GPIO latch at a cost of one extra pin
and losing the hardware-timed pulse; NSSP's specific contribution is removing
that one pin and the software toggle, not the axis-count independence itself.
Inputs aren't affected by any of this — probe/limit/e-stop still need
individual GPIO, or a separate input-shift scheme via the same SPI's MISO
line, not needed given the margin above.)

Candidates:

- **STM32F103C8T6** ("Blue Pill" class, 48-pin LQFP) — the chip Airedale
  actually ships with, confirmed. No NSSP. Fine for prototyping (§13) with a
  software-toggled latch; not the production chip either for pin-count
  reasons under the old per-signal-GPIO assumption (37 GPIO, ~25-29 free after
  fixed overhead) or now for lacking NSSP.
- **STM32G030C6T6** (48-pin LQFP) — **44 GPIO, ~33 free**, 2 USART, has NSSP,
  Cortex-M0+ @ 64MHz, 32KB flash/8KB RAM. Current leading production
  candidate given the SPI-shift output architecture — clears the GPIO target
  in the 48-pin package, no need to step up to 64 pins at all. 32K/8K is
  plausibly enough for firmware this deliberately lean (no `step_engine_t`
  abstraction, no AMASS, direct register manipulation, message queue sized in
  messages rather than steps thanks to §10's compression) — `STM32G030C8T6`
  (identical part, 64KB flash) is the drop-in fallback, same package/pinout,
  if flash turns out tighter in practice.
- **STM32F103RCT6** / **STM32F051R8T6** (64-pin, 51/55 GPIO) — still valid if
  more margin is wanted (more spare inputs, future axis growth, accessories)
  or if NSSP turns out not to matter as much as expected in practice, but no
  longer required the way they were under the direct-GPIO-per-step/dir-signal
  assumption.
- **STM32C071CBT6** — Cortex-M0+ @ 48MHz, 48-pin LQFP, 2 USART, 128KB
  flash/24KB SRAM. Newer/cheaper alternative to G030 if it also has NSSP (not
  yet confirmed here) — same-generation SPI peripheral as G0, so likely, but
  worth checking before committing.

None need M4/M7 headroom or advanced-timer parts — the step-generation load is
a single shared timer ISR doing software Bresenham (mirroring FluidNC's own
"Timed" engine), not one hardware timer channel per axis.

## 13. Prototyping path: Airedale

The existing Airedale STM32 IO expander (`fluidnc-wiki-content/hardware/official/airedale.md`,
firmware/protocol/hardware all by the same author as this design) is a strong
prototyping vehicle, not just an analogy:

- Same daisy-chain topology already shipping ("To FluidNC" / "To Pendant" RJ12,
  documented forward-everything behavior).
- Same link speed already in production (`baud: 1000000` in its sample config).
- Sub-ms input latency already documented and already scoped for probes/limits.
- The real "Channel I/O" wire protocol (`config/uart_sections.md`) already
  establishes the UTF-8-codepoint-banded convention this design's framing
  extends (§9).

Caveat: the board's documentation currently states outputs "cannot be used for
motion control" — this reflects that remote stepping doesn't exist yet, not a
hardware ceiling. Prototyping it means new/forked firmware, and the physical
board exposes ~21 addressable channels versus the ~32 GPIO a full 6-axis product
would need — enough for a reduced (e.g. 2-axis) prototype to validate the
credit/underrun protocol and the probe-latency improvement, not a full-scale
final product. Confirmed: Airedale uses STM32F103C8T6 (48-pin), which after
SWD + 2× USART + crystal overhead doesn't leave enough GPIO for a real 6-axis
product either way — good enough for prototyping exactly as-is, but the
production board needs the larger-package part in §12
(`STM32F103RCT6`, same family/toolchain, more raw pins).
