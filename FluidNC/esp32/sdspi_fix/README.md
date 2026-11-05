# sdspi_fix — sdspi_host.c with an allocation-free, RX DMA-aligned receive path (IDF 5.5.6)

## Why

SPI RX DMA requires the receive buffer address, and on some targets its length,
to be aligned (4 bytes on `esp32` and `esp32s3`; the cache line size on the
cached-internal-RAM ESP32-P4). When it is not, spi_master's
`spicommon_dma_setup_priv_buffer()` allocates a temporary internal DMA buffer
for that transaction and frees it afterwards.

Stock `sdspi_host.c` uses unaligned RX lengths almost everywhere:

| Transaction | RX bytes |
|---|---|
| `release_bus()` (after every command) | 1 |
| `poll_busy()` (per poll) | 1 |
| `start_command_default()` | 15 (R1/R1b), 16 (R2), 19 (R3/R4/R7) |
| `poll_data_token()` (unaligned `uint8_t` stack array) | 8 |
| single-block read data (`CMD17`, last block of `CMD18`) | 514 |
| write CRC + data response | 3 |
| `go_idle_clockout()` | 10 |

Up to IDF v5.5.4 the length was checked on every target, so every sector read
did several malloc/free pairs from internal DMA memory, including a 516-byte
one. Under low/fragmented heap the allocation failed, and on <= v5.5.5 the
failure path then `memcpy`d from NULL (LoadProhibited panic; fixed in v5.5.6 by
`6c1f0e7b59`).

From v5.5.5 (`0a752844eb`) only the address is checked on targets without
cache sync, so on esp32/esp32s3 stock sdspi mostly stops allocating per
transaction. What this copy still buys on v5.5.6:

- `block_buf` is allocated once at device init instead of on the first read.
  Stock allocates it lazily, wherever the heap has room at that moment, and
  keeps it for the life of the device; measured on esp32s3, first SD use then
  cost ~860 bytes and cut the largest free block by 3 KB.
- `poll_data_token()`'s stack buffer is gone, so no per-poll allocation when
  it happens to be unaligned.
- The block-read stream parser fixes a latent bug: stock `extra_data_ptr`
  points into `block_buf`, which the next receive can overwrite before the
  extra bytes are consumed (needs a card with no gap between blocks).
- On ESP32-P4 the length is still checked against the cache line, so stock
  sdspi allocates on nearly every transaction there.

## Changes vs v5.5.6

- The slot's `block_buf` is allocated once in `sdspi_host_init_device()`, aligned
  to and sized as a multiple of the bus's RX DMA alignment. That alignment is read
  from the SPI driver the same way `setup_dma_priv_buffer()` computes it:
  `MAX(spi_bus_get_dma_ctx(host)->dma_align_rx_int, spi_bus_get_attr(host)->cache_align_int)`
  (1 when the bus has no DMA). It is 4 on esp32/esp32s3, and the cache line size on
  targets whose internal RAM is cached (ESP32-P4).
- Every receive goes through `rx_transfer()`, which copies the TX bytes into
  `block_buf`, pads the transfer with 0xff to the alignment, and leaves the
  received bytes there for the caller to copy out. So spi_master never needs a
  temporary buffer, and no read/write path can fail with `ESP_ERR_NO_MEM`.
- `release_bus()`, `go_idle_clockout()`: TX only (the received bytes were unused).
- `start_command_default()`, the write command, and the write CRC/data-response
  use `rx_transfer()`. Padding clocks 0xff after the response, or while the card
  is busy after a write.
- `poll_busy()` uses `rx_transfer()` and counts non-zero bytes across the whole
  padded read.
- `start_command_read_blocks()` is rewritten as a byte-stream parser
  (`read_stream_feed()`): the bytes after R1, token polls, and data transfers all
  feed one state machine (wait for token -> data -> CRC). Bytes read ahead,
  including padding, are consumed in order and never lost. This replaces the
  `pre_scan`/`extra_data` bookkeeping and `poll_data_token()`, and makes
  v5.5.6's oversized-extra-data check (`b99459931a`) unnecessary.
- The final block of a read still requests no extra bytes after the CRC (see
  esp-idf `4a2489b99a`: extra bytes before STOP_TRANSMISSION confused some cards).
  `SDSPI_PAD_FINAL_BLOCK` (default 0) selects what happens there:
  - `0`: the transfer ending with the final CRC of a multi-block read
    (`CMD18`) is done at its exact length, as upstream does. spi_master then
    allocates a temporary buffer for it only on targets that check the
    length (ESP32-P4): once per multi-block read. On esp32/esp32s3 with v5.5.6
    the aligned `block_buf` address is enough, so neither setting allocates.
  - `1`: that transfer is padded like the rest, clocking up to `rx_align - 1`
    extra bytes before STOP_TRANSMISSION. No allocations at all, but it
    reintroduces the extra clocks that `4a2489b99a` removed.

  Build the `1` variant with `PLATFORMIO_BUILD_FLAGS=-DSDSPI_PAD_FINAL_BLOCK=1`.
  It only matters on targets that check RX length (P4); test it with several
  cards before making it the default there.

`diff` against the v5.5.6 tag to see the exact changes:

    gh api 'repos/espressif/esp-idf/contents/components/esp_driver_sdspi/src/sdspi_host.c?ref=v5.5.6' \
        -H 'Accept: application/vnd.github.raw' | diff - sdspi_host.c

`sdspi_private.h` and `sdspi_crc.h` are verbatim v5.5.6 private headers
needed to compile it.

## How

The object is a direct link input, so all `sdspi_host_*` symbols resolve from it
and `libesp_driver_sdspi.a(sdspi_host.c.obj)` is never pulled. Because the
esp32 envs link with `-z muldefs`, a duplicate would not be a link error:
verify the linker map has no `libesp_driver_sdspi.a(sdspi_host.c.obj)` entry.

The file compiles to nothing unless `ESP_IDF_VERSION == 5.5.6`, so a platform
bump silently falls back to the stock driver; re-port it then if upstream has
not fixed the lengths.
