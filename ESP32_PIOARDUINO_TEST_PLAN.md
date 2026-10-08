# Hardware test plan: classic esp32 on pioarduino 55.03.39

Branch `DevelopToolchain`, commit `b71e496f`. This moves the `wifi`, `bt` and `noradio` envs from Arduino 2.0.17 (IDF 4.4.7, GCC 8.4) to Arduino 3.3.9 (IDF 5.5.4, GCC 14.2).

Everything below has only been built and link-checked so far. Nothing has run on hardware.

## What changed and what each change puts at risk

| Change | Risk | Covered by |
|---|---|---|
| IDF 4.4 → 5.5, GCC 8 → 14 | Anything; new drivers, new libstdc++, new Wi-Fi blobs | All sections |
| `.eh_frame` moved to its own output section | C++ exceptions fail to unwind, so `throw` aborts instead of reaching `catch` | §3 |
| `stdfs17` replaces `stdfs` | `std::filesystem` behavior on LocalFS and SD | §4 |
| `fatfs_tiny` (FF_FS_TINY, IDF 5.5.4 FatFs R0.15) | SD data corruption when two files are open at once | §4, §9 |
| `lib_archive = yes` | A library that's actually used is now left out, giving a missing feature rather than a link error | §5, §6 |
| `ppp_stub.c` | None expected; PPP is never used | §2 boot log |
| `unused_stubs.cpp`: core dump | The panic path changes; the serial backtrace must still appear | §3 |
| `unused_stubs.cpp`: provisioning deinit, chip report | None expected | §2 |
| New `sections.ld` with `vtable_in_dram.ld` | Vtables back in flash, which causes the cache crash during ISR + flash access | §7 |
| Legacy I2C and RMT drivers on IDF 5 | Conflict-check abort at boot (verified absent in the ELF) | §2 |
| Installer `firmware-update` now also writes the bootloader (`0x1000`) and `boot_app0` (`0xe000`) | The bootloader write fails or is interrupted; settings or files get wiped by mistake | §1 |

## 0. Baselines to capture first, on Arduino 2.0.17 (current `main`)

Use the same board and config file throughout. Record:
- [ ] Free heap and largest free block at idle after boot (`$Heap` or the startup log).
- [ ] The same two numbers with WebUI open and one SD file streaming.
- [ ] Peak step rate that still gives clean pulses on the scope, plus step pulse width and direction-setup time, for each stepping engine you use (RMT, I2S, timed).
- [ ] Boot-to-ready time.

## 1. Flashing and upgrade paths

Upgrades can leave the bootloader and the app on different IDF versions. These are the combinations to cover:

| Bootloader | App | How a board gets there |
|---|---|---|
| IDF 5 | IDF 5 | Fresh install; installer `firmware-update` with the new manifest |
| **IDF 4** | IDF 5 | WebUI OTA from a 2.0.17 release (the bootloader is never touched by OTA) |
| IDF 5 | **IDF 4** | Installer `firmware-update` back to an older release (old manifests don't list the bootloader), or WebUI OTA back to 2.0.17 after a new-manifest install |

To check which bootloader a board has, look at the first boot-log line from the second-stage bootloader, which prints its ESP-IDF version.

**Fresh install**
- [ ] **Installer `fresh-install`** of the `wifi` build: boots.

**Installer `firmware-update` with the new manifest** (writes bootloader + `boot_app0` + firmware, no erase):
- [ ] From a board running 2.0.17: boots, and the boot log now shows the IDF 5 bootloader.
- [ ] NVS settings are kept (Wi-Fi SSID and password, `$` settings).
- [ ] LittleFS contents are kept and readable (config file, macros, WebUI files).
- [ ] **Starting from a board whose last update was a WebUI OTA**, so it's running from `app1`: after `firmware-update`, the board runs the *new* firmware. Before this change, it kept booting the old firmware from `app1`.
- [ ] **Interrupted bootloader write:** unplug USB during the first image (the bootloader). The board won't boot, but it still enters download mode (hold BOOT if needed); re-running `firmware-update` or `fresh-install` recovers it with settings and files intact.
- [ ] Repeat once for `bt` and once for `noradio` (same bootloader image, different firmware).

**Release-zip install scripts** (`install-wifi` / `install-bt`). These write bootloader + `boot_app0` + firmware + partitions without erasing, and unlike the web installer they force `--flash-mode dio --flash-freq 80m --flash-size detect`, so esptool rewrites the IDF 5 bootloader's header:
- [ ] `win64\install-wifi.bat` on a board running 2.0.17: boots; the boot log shows the IDF 5 bootloader; settings and LittleFS are kept.
- [ ] `posix/install-wifi.sh` (macOS or Linux), same checks.
- [ ] `install-bt` on one platform: boots.
- [ ] After a script install, the boot log's SPI mode and speed lines show DIO / 80 MHz, and a WebUI page load plus an SD job run cleanly (flash is working at the forced settings).
- [ ] `install-fs` (LittleFS only) on top of a script install: WebUI loads.

**IDF 4 bootloader + IDF 5 app** (WebUI OTA path):
- [ ] **OTA from a 2.0.17 release** to this build through WebUI: boots. The boot log still shows the IDF 4 bootloader.
- [ ] After that OTA: NVS settings are kept.
- [ ] After that OTA: LittleFS contents are readable. The littlefs library version changed.
- [ ] Run the §2 boot checks and a short §5 / §7 smoke test in this configuration, not only with the matched bootloader.
- [ ] A second WebUI OTA (this build → this build) while on the IDF 4 bootloader: boots.

**IDF 5 bootloader + IDF 4 app** (downgrade path):
- [ ] On a board that has the IDF 5 bootloader (from `fresh-install` or new-manifest `firmware-update`), **WebUI OTA to a 2.0.17 release**: boots, and the boot log shows the IDF 5 bootloader starting the 2.0.17 app.
- [ ] Same board: **installer `firmware-update` to an older release** whose manifest lists only the firmware: boots.
- [ ] In this configuration, 2.0.17 still mounts LittleFS after the new firmware has written to it. A newer littlefs can bump the on-disk minor version on write.
- [ ] Wi-Fi, SD and a short motion test on 2.0.17 behave normally (the IDF 4 app is running on an IDF 5 bootloader's flash and clock setup).

**OTA robustness**
- [ ] Interrupt a WebUI OTA midway (power off); the previous image still boots.

## 2. Boot and startup log

- [ ] Clean boot on `wifi`, `bt` and `noradio`.
- [ ] **No** `CONFLICT! driver_ng is not allowed` abort, for either I2C or RMT.
- [ ] *Expected:* legacy-driver warnings for I2C and RMT along the lines of "This driver is an old driver, please migrate…", if early logging is visible. These are harmless.
- [ ] No messages about core dump, PPP or provisioning.
- [ ] Idle heap and largest block compared with the §0 baseline. Note the delta; IDF 5 is expected to use somewhat more RAM.
- [ ] Task stack high-water marks (DEBUG_HEAP build if handy). Validate them under load, not at idle.

## 3. Exceptions and panic path

- [ ] **Caught exception:** load a config with a deliberate YAML error (bad value type, unknown section). Expect FluidNC's parse error message and continued operation, **not** an abort or `terminate called`. This is the key check for the `.eh_frame` move.
- [ ] A second throw site, for example a `$` command that reports an error through an exception path, or a filesystem error on a missing path.
- [ ] **Forced panic** (null dereference or whatever test hook you use):
  - [ ] A Guru Meditation register dump and backtrace appear on serial.
  - [ ] The backtrace decodes with `tools/stack_trace_decoder` against this build's ELF.
  - [ ] No core-dump write attempt; the board reboots normally.
- [ ] Task watchdog: provoke a stall and confirm the log-only TWDT report still appears with no panic (#1881 behavior).

## 4. Filesystems

**LocalFS (LittleFS):**
- [ ] List, upload, download, rename, delete and mkdir through WebUI and `$LocalFS/...`.
- [ ] `$LocalFS/Format`, then re-upload WebUI.
- [ ] Byte-for-byte check: upload a ~200 KB file, download it, compare sha256.

**SPIFFS:** only if any supported board still uses it. Mount a SPIFFS-formatted partition and list and read it.

**SD (`fatfs_tiny`, FF_FS_TINY = 1):**
- [ ] Mount, list (with subdirectories and long names), `$SD/Run` a job.
- [ ] Byte-for-byte check: upload a large file (a few MB) through WebUI, download it, compare sha256.
- [ ] **Interleaved access**, the main FF_FS_TINY risk because all open files share one sector window:
  - [ ] Run a job from SD while repeatedly listing the SD directory from WebUI.
  - [ ] Run a job while downloading a different SD file through WebUI.
  - [ ] Run a job while uploading a different file to SD.
  - [ ] After each one: the job's G-code stream shows no corruption (no garbage lines or skipped lines), and the transferred file's sha256 matches.
- [ ] Rename and link paths (`vfs_fat_link` allocates two temporary FIL structs).
- [ ] Heap check: the heap drop when opening a file should now be well under 4 KB per open file. Compare with the §0 baseline.
- [ ] Remove and reinsert the card, then remount.

## 5. Networking (`wifi` env)

- [ ] STA on a WPA2-PSK network.
- [ ] STA on a **WPA3-SAE** network (and WPA2/WPA3 mixed).
- [ ] AP mode: phone connects, gets a DHCP address, WebUI loads (exercises the SoftAP auth and DHCP server).
- [ ] STA failure falls back to AP.
- [ ] Reboot the access point; STA reconnects.
- [ ] Wi-Fi scan through WebUI / ESP410: results come back with no watchdog trip.
- [ ] mDNS: `http://<hostname>.local` resolves.
- [ ] NTP: time syncs (`configTime`).
- [ ] WebUI: page load, WebSocket status updates (`SHA1Builder` handshake), file manager.
- [ ] Telnet session.
- [ ] WebDAV mount and copy.
- [ ] `$HTTP` / `HttpCommand` to a plain-HTTP endpoint (`NetworkClient`).
- [ ] **Notifications over TLS** (`NotificationsService` → `NetworkClientSecure`): send a test notification and confirm it arrives. This is the only TLS client in the image.
- [ ] OTA firmware update through WebUI (`Update` library), then a second OTA.
- [ ] ESP-NOW pendant channel, if you have the hardware.

## 6. Bluetooth (`bt` env)

- [ ] Pair a BT serial client, stream G-code, run a short job.
- [ ] Disconnect and reconnect.
- [ ] Heap at idle compared with the 2.0.17 `bt` build.

## 7. Motion, ISRs and vtables in DRAM

- [ ] **The vtable crash test:** start a long G-code move, then load WebUI or do heavy LocalFS access while it runs. Repeat several times. Expect no crash. If the vtables had landed back in flash, this would trigger the cache crash.
- [ ] Each stepping engine you support (RMT, I2S, timed): scope step and direction signals; pulse width, direction setup and peak step rate match the §0 baseline.
- [ ] Homing and limit switches (GPIO ISRs).
- [ ] Spindle PWM (LEDC): frequency and duty on the scope.
- [ ] Modbus VFD over UART/RS485 (`fnc_idf_uart_5_5_4` path): start, stop, speed and direction; RS485 direction-pin timing on the scope.
- [ ] TMC2209 over UART and TMC over SPI: driver detection and config readback.
- [ ] UART pendant channel.

## 8. I2C devices (legacy driver on IDF 5)

- [ ] SSD1306 OLED shows the status screens.
- [ ] I2C pin extender, if any supported board uses one.

## 9. Soak

- [ ] An 8+ hour job from SD with WebUI open and polling: no resets, no SD errors, stable heap (log the largest block periodically).
- [ ] 24 hours idle on Wi-Fi with WebUI connected: no resets or disconnects.

## 10. `noradio` env

- [ ] Boot, serial console, SD job, motion, all with no radio.

## Pass criteria

All of §1–§8 pass on at least one `wifi` board and one `bt` board, §9 completes without incident, and the heap and timing deltas against §0 are understood and acceptable.

## If something fails

- **A bootloader/app mismatch fails to boot:** note which combination and what the boot log shows. If IDF 4 bootloader + IDF 5 app fails, WebUI OTA can't be a supported upgrade path for this release. Users would have to update with the installer, and the release notes would have to say so.

- **Exceptions abort instead of being caught:** check `__eh_frame` / `__eh_frame_end` in the ELF, and try reverting just the `.eh_frame` relocation in `FluidNC/ld/esp32/<env>/sections.ld`.
- **SD corruption:** rebuild without `fatfs_tiny` (comment the two `+<…_tiny.c>` lines in `[common_esp32]`) to separate the FF_FS_TINY effect from the IDF 5 FatFs change.
- **A missing feature:** a library may have been dropped by `lib_archive = yes`; check the linker map.
- **Any suspicion about a stub:** since `-z muldefs` hides duplicate definitions, check the map for the archive members the stub files are meant to keep out (`libespcoredump`, `liblwip.a(ppp…)`, `esp_netif_lwip_ppp`, `network_provisioning`, `chip-debug-report`).
