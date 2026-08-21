# FluidNC v4.0.4 Web resource soak

This runbook measures the board's **dynamic** safe WebSocket capacity while
leaving the firmware's absolute cap of eight and all heap admission guards
enabled. It does not assume that eight clients are safe. The measured capacity
is the number admitted under the observed heap/fragmentation state; later
handshakes must be rejected without a panic when the reserve is gone.

## Safety contract

The harness permits only:

- HTTP `GET` reads for state, diagnostics, configuration fingerprints and
  static files;
- RFC6455 upgrades on `/`;
- WebSocket Pong/Close control frames; and
- local TCP close.

Two bounded recovery probes additionally (a) send a complete WebSocket upgrade
and immediately reset that client TCP connection before reading the response,
and (b) hold eight deliberately incomplete HTTP headers for two seconds. These
exercise abandoned pending-upgrade and half-open request cleanup; neither sends
a request body or application command.

It contains no G-code, homing, jog, macro, upload, reset, power-cycle,
configuration-write or preferences-write route. Every capacity connection
uses `independent_session=1`; a separate stage uses two plain `/` connections
to prove the intended same-session `newest wins` cleanup. No WebSocket text or
binary application frame is ever sent.

The run fails closed unless all of these are true before the first pressure
step:

- the machine is exactly `Idle`;
- `[ESP420]` reports hardening ID
  `v4.0.4-webhardening-20260821-r22` and a positive boot sequence;
- pending, active, connecting, deferred WebSockets and file streams are zero;
- free heap and largest block meet the first-socket admission minima; and
- all required HTTP/AsyncTCP/resource counters are present.

Fast guards abort during a stage on a state change, boot sequence change,
telemetry counter reset, resource-accounting mismatch, stuck slot,
stale-client timeout or loss of reachability. Full build/config/runtime/
backtrace/journal fingerprints are re-read after each completed stage; a drift
aborts before the next pressure stage. The tool never reboots the board after
an abort.

Preflight requires the normal first-WebSocket raw heap floor: 28 KiB effective
free plus the 6 KiB pending reservation, or 34 KiB observed free, together with
a 20 KiB largest block.  The lower 24 KiB effective recovery floor is tested
only by the explicit poll-free recovery barriers; it is not used to admit a
degraded starting point to a complete soak.

## Profiles

`parallel-repro` performs only 100 concurrent eight-handshake bursts.  It has
the same preflight, identity and postflight guards as the other profiles, but
does not first run churn, partial HTTP, stale or static traffic.  Use it after
a `soak` abort at `parallel-handshakes`: every HTTP 000 is persisted before
the fail-closed abort with the client-side category (`eof_before_http_status`,
`invalid_http_status` or `transport_exception`), response-byte count, elapsed
time, and matching AsyncTCP RX-timeout/admission-rejection deltas.  This is a
diagnostic reproducer, not a capacity override and it still sends no WebSocket
application data.

The WiFi build also reports `TCP accept PCB active+TIME_WAIT peak`. It is an
lwIP accept-callback high-water sample across active and `TIME_WAIT` PCBs. It
does not measure a TCP capacity, pending SYNs, `bound` PCBs, or allocations
that fail before `tcp_accept`; it must therefore never by itself be used to
attribute a client timeout to a WebSocket leak, a TCP limit, or a crash.

The hardened resource-admission reject itself is intentionally an exception:
instead of allocating a status-only HTTP response while the heap guard has
already refused the handshake, it issues a direct TCP abort.  The harness
accepts that only when it is a zero-byte EOF/reset in at most one second and
the combined heap/client-limit counter rises by exactly the number of rejected
handshakes.  A client timeout, bytes of malformed response, a counter mismatch,
or any other transport exception remains an `ABORT`.

The r20 image additionally reports `Async WebSocket reject abort calls` and
`Async WebSocket reject abort max us`.  These counters do not alter admission
or timing: they distinguish a late TCP/WebSocket arrival from time spent inside
the already-selected synchronous transport abort after a fail-closed reject.

`smoke` performs the complete state machine with 20 connect/close iterations,
four eight-client connect-then-immediate-RST rounds, one aborted-upgrade burst,
one eight-client partial-HTTP round, three parallel eight-handshake bursts, one
deliberately no-Pong client and three mixed eight-request static bursts. Run
this first after USB bootstrap.

The partial-HTTP stage starts from the previous poll-free identity checkpoint;
it does not create a telemetry client immediately before occupying the cap.
Each round keeps all eight incomplete transports pending for the full hold
interval, performs one deliberate ninth read-only `$State` request, requires
that transport to be rejected, closes the eight held sockets and only then
reads ESP420.  Every round must contribute exactly one matching `Async TCP
accept admission rejections` increment.  A 500-ms poll-free release grace after
that ESP420 response prevents the observer itself from occupying a slot in the
next burst.  Complete pending/client/event recovery remains mandatory.  A raw
reset from the ninth probe is expected evidence, not a harness abort.

The stale stage records the actual RFC6455 Ping payload and accepts a server
close as heartbeat eviction only after observing `Ping/FNC` and the configured
minimum age.  The static stage configures a 1-KiB client receive buffer before
connect and detects the first response byte with `MSG_PEEK`; the worker remains
bounded on a release event while the light `$State` response must still complete.
After release, monotonic `Web file starts` and `Web file completions` must each
advance by exactly one.  The transfer then has to finish as a complete HTTP 200
response with a non-empty body (including declared Content-Length/chunked
completeness).  Heavy ESP420 telemetry is intentionally not admitted during an
active FileStream.  Mixed WS/static pressure runs only after 125 poll-free
seconds, one mandatory recovery WebSocket, and a fresh capacity discovery; any
capacity loss is recorded instead of being hidden or compared blindly with the
pre-stream capacity.  Final lifecycle gaps must equal their baseline gaps
exactly; even one new persistent request, owner, client or event fails postflight.

`soak` uses 500 connect/close iterations, 50 eight-client immediate-RST rounds,
25 partial-HTTP rounds, 100 parallel eight-handshake bursts, two stale clients
and 50 mixed static bursts. This is the hard boundary run.

Both profiles first attempt sequential targets 1 through 8 without disabling
protection. Each non-101 result must correlate with the firmware's heap- or
client-limit rejection counters. `capacity` in the JSON report is the largest
simultaneously admitted count, not the compile-time cap.

After every full identity checkpoint, the harness leaves the board completely
unpolled for 125 seconds and then makes exactly one abortively closed WebSocket
upgrade. This distinguishes transient TCP/HTTP backpressure from a persistent
resource-accounting failure: diagnostic GET requests themselves create short
TCP lifetimes and therefore must not be used as a recovery poll loop. A stage
passes only when that one post-quiet probe receives HTTP 101. Closing that
one-shot probe restarts r15's 60-second first-client recovery timer, so the
harness then remains completely unpolled for another 65 seconds before the next
stage. Without this rearm interval, the diagnostic probe itself can consume the
single recovery admission and make the next stage produce a valid but
misleading HTTP 401.

The r15 WebSocket admission policy reserves 6 KiB before considering the first client and
normally requires 28 KiB effective free plus a 20 KiB largest block. If all
WebSocket resources have remained at zero for more than 60 seconds, exactly the
first client may use a separately
counted 24 KiB recovery floor; this still leaves the 6 KiB socket reservation
and the 20 KiB contiguous-block guard intact. Every additional client reserves
7 KiB and requires 32 KiB effective free; the absolute cap remains eight.
Consequently the live capacity may be two or three even though the first UI can
self-recover after a fragmenting burst.

r15 permits only one FileStream.  Admission requires 44 KiB free and a 12 KiB
largest block; an active stream continues to reserve 12 KiB against WebSocket
admission.  `[ESP420]`/`$ESP420` responses use a separately owned Heavy-HTTP
slot with a 32 KiB/12 KiB free/largest guard.  FileStream and Heavy HTTP are
mutually exclusive under the same allocation-free lock, while `$State` remains
a light command.  The Heavy slot transfers to `WebClient` only after successful
construction and is released by its destructor after disconnect/background
cleanup; a full channel-reap queue is retried from the main WebUI poll through a
fixed deferred array.  This closes the measured Static+ESP420 peak that reduced
minimum free heap to 4.11 KiB without replacing it with a blanket 48 KiB floor.

r15 retains the r11 AsyncTCP accept-OOM owner fix: if the small queue
event cannot be allocated after an `AsyncClient` was created, the PCB callbacks
are detached, the PCB is aborted and the unpublished client is destroyed in
that order. `Async TCP accept event allocation failures` counts this recovered
path. A nonzero delta is acceptable only when the TCP client lifecycle gap does
not grow with it.

r15 also gates raw server accepts before `AsyncClient` allocation. The hard
transport cap is eight live clients, but the practical limit is dynamic: a
first transport requires 24 KiB free, later transports require 32 KiB, every
queued-but-unpublished accept reserves 7 KiB, and the largest contiguous block
must remain at least 20 KiB. This is intentionally earlier than HTTP and
WebSocket parsing. `Async TCP accept admission rejections` counts clean early
refusals and `Async TCP server pending accepts` must return to zero.
The hard cap uses the process-wide `AsyncClient` lifecycle counters and is
therefore intentionally conservative. The reviewed WiFi image constructs only
the WebUI `AsyncServer`; adding a second server or outbound AsyncTCP client in a
future build would share these eight slots and must trigger a policy review.

r15 additionally closes the early-RST ownership gap: if lwIP reports an error
before the queued ACCEPT event has published its `AsyncClient`, queue cleanup
marks that ACCEPT as removed, clears the already-freed PCB pointer, destroys the
unpublished client exactly once, and does not enqueue an ERROR event containing
the freed pointer. Repeated connect-and-RST pressure must therefore leave both
the pending-accept count and the TCP client lifecycle gap at their baselines.
The stage also requires a positive `Async TCP early RST accept cleanups` delta,
so a PASS proves that the pre-dispatch ownership branch actually ran rather
than merely observing later, already-published disconnects.

## Commands after the one-time USB bootstrap

Close FWC and every FluidNC WebUI tab first so the preflight sees zero existing
WebSockets. Do not run this while the machine is moving.

Before any OTA candidate is staged, verify it against the **same** PlatformIO
core used for that build.  A global PlatformIO cache is not an acceptable
substitute: another local build can mutate or replace its packages without
changing the firmware worktree.  For a project-local r17 core, for example:

```powershell
$python = '<python>'
$platformio = '<platformio>'
$root = '<firmware-worktree>'
$core = "$root\.pio-core-r17"
& $python "$root\tools\build_debug_firmware.py" `
  --root $root `
  --core-dir $core `
  --platformio $platformio
$env:PLATFORMIO_CORE_DIR = $core
& $python "$root\tests\diagnostics\verify_debug_firmware.py" --root $root
```

The wrapper first cleans and rebuilds WiFi under the explicit isolated core,
then writes creation-only input and artifact receipts. The verifier
intentionally fails if the selected core, source commit, input manifest or
BIN/ELF differs from those receipts or the recorded dependency receipt. A
passing build verifier is necessary evidence, but it is not permission to
flash or a substitute for the read-only board soak below.

```powershell
$python = '<python>'
$root = '<firmware-worktree>'
$artifactRoot = '<artifact-root>'

& $python "$root\tools\soak_fluidnc_web_resources.py" `
  --host 192.0.2.2 `
  --output "$artifactRoot\web-parallel-repro" `
  --profile parallel-repro `
  --max-connections 8 `
  --expected-hardening-id v4.0.4-webhardening-20260821-r22 `
  --execute-live

& $python "$root\tools\soak_fluidnc_web_resources.py" `
  --host 192.0.2.2 `
  --output "$artifactRoot\web-soak-smoke" `
  --profile smoke `
  --max-connections 8 `
  --expected-hardening-id v4.0.4-webhardening-20260821-r22 `
  --execute-live

& $python "$root\tools\soak_fluidnc_web_resources.py" `
  --host 192.0.2.2 `
  --output "$artifactRoot\web-soak-full" `
  --profile soak `
  --max-connections 8 `
  --expected-hardening-id v4.0.4-webhardening-20260821-r22 `
  --execute-live
```

Each output directory is creation-only and contains `events.jsonl` plus an
atomic `soak-report.json`. Existing evidence is never overwritten. Expected,
unexpected and user-interrupt paths all attempt to persist an `ABORT` report
before cleanup; only an output-device failure can prevent that best-effort
write. An `ABORT` report is diagnostic evidence, not a PASS.

## Acceptance

Accept a profile only when `soak-report.json` says `PASS`, the boot sequence and
all identity hashes are unchanged, all resource slots return to zero, lifecycle
gaps return near baseline, the AsyncTCP queue drains, the board remains Idle,
every 125-second poll-free barrier regains one HTTP 101, and no new crash-journal
content appears. A capacity below eight is valid when heap admission rejected
the rest and the board recovered; raising the cap is not the objective.
