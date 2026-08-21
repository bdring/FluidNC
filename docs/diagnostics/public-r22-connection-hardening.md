# ESP32 WebUI connection hardening — R22 debug preview

This branch is a reviewable engineering preview for a FluidNC v4.0.4-based
ESP32 WebUI connection failure investigation. In the reported failure mode,
repeated WebUI requests and concurrent WebSocket activity could exhaust or
fragment the heap; the controller then became unreachable during a machining
operation. That is a safety concern because a controller that stops servicing
the control path must not be treated as a normal pause.

## What changed

- bounded WebUI/WebSocket admission and cleanup paths;
- allocation-safe request/file/stream guards;
- explicit stale-client and early-RST ownership cleanup;
- AsyncTCP accept-event cleanup and diagnostic counters;
- Channel semaphore lifetime/unwind repair;
- bounded OTA/error handling and recovery telemetry;
- read-only diagnostics and host-side regression tests.

The production capability flags for newer recovery features remain disabled in
this debug preview. No generic G-code relay, motion command, configuration
write, or automatic resume is enabled by this branch.

## Evidence for the published R22 artifact

- 100 parallel bursts of 8 connection attempts: PASS;
- complete 9-stage read-only WebUI lifecycle smoke: PASS;
- static pressure: excess requests were rejected deliberately; the board
  stayed responsive and recovered to the normal admission policy;
- no reboot beyond the deliberate OTA update, and configuration/runtime/
  journal/backtrace hashes remained unchanged in the controlled run;
- BIN SHA-256: `C9CA9DCA4AD809DCE473601C1110D113471181A274654E50A3CF9C3BC3A16392`;
- BIN size: 1,828,864 bytes.

These results are a bounded diagnostic result, not a claim that every board,
transport, browser, or workload has one universal safe connection count. The
firmware should be reviewed and split into upstream-appropriate changes before
any production release.
