// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// This is a tool for tracking down the source of memory leaks by letting you
// inspect heap blocks that were allocated after some event.
// Run $Heap/Snapshot AKA $hs before and $Heap/Diff AKA $hd after.
// Snapshotting creates a list of the heap blocks currently in use,
// diffing compares the current heap to that list, showing the
// first few bytes of blocks that newly appeared.
// It uses enough memory that it should not be included in production builds.
// It only works on older ESP-IDF versions like 4.4, per comments below.
// Later ESP-IDF versions have an API for traversing the heap.

#include "HeapDiff.h"

#ifdef HEAPDIFF

#    include "Logging.h"
#    include "Channel.h"
#    include "Protocol.h"  // drain_messages()

#    include <esp_heap_caps.h>
#    include <cstdlib>  // qsort
#    include <cstring>  // memset
#    include <cstdio>   // snprintf
#    include <cstdint>

// ---------------------------------------------------------------------------
// Home-grown heap-block snapshot / diff.
//
// The IDF 4.4 that this env builds against has no public per-block heap
// iterator (heap_caps_walk() is IDF 5.1+), and heap_caps_dump() watchdogs
// because it does UART I/O while holding the heap spinlock.  So we drive the
// block iteration ourselves via `registered_heaps` (a COMMON symbol) and the
// exported-but-unheadered multi_heap_get_*_block() API.
//
// RAM budget matters here -- this runs on a board that is already close to OOM
// during a WebUI load.  The baseline snapshot stores only a 24-bit offset per
// used block (offset from DRAM_LO), so CAP_BLOCKS entries cost 3*CAP_BLOCKS
// bytes.  Block size is NOT retained in the baseline, so "resized in place"
// (same address, different size) is not detected -- only added / freed.  A
// TLSF realloc almost always moves the block, which shows up as freed+added
// anyway.
// ---------------------------------------------------------------------------

namespace {

    // ---- IDF 4.4 internal heap_t (components/heap/heap_private.h) ----------
    // Offsets verified by disassembling heap_caps_get_info(): ->heap @28,
    // ->next @32.  The 8-byte gap before ->heap is an embedded portMUX_TYPE.
    struct idf_heap_t {
        uint32_t           caps[3];      // @0
        intptr_t           start;        // @12
        intptr_t           end;          // @16
        uint32_t           heap_mux[2];  // @20  portMUX_TYPE (opaque here)
        void*              heap;         // @28  multi_heap_handle_t
        struct idf_heap_t* next;         // @32  SLIST_ENTRY.sle_next
    };

    extern "C" {
    // SLIST_HEAD(..., idf_heap_t) registered_heaps;  -- one pointer, COMMON
    extern struct {
        struct idf_heap_t* slh_first;
    } registered_heaps;

    // Exported from libheap.a, absent from the shipped headers.
    typedef void* mh_handle_t;
    typedef void* mh_block_t;
    mh_block_t    multi_heap_get_first_block(mh_handle_t heap);
    mh_block_t    multi_heap_get_next_block(mh_handle_t heap, mh_block_t block);
    bool          multi_heap_is_free(mh_block_t block);
    void*         multi_heap_get_block_address(mh_block_t block);
    void          multi_heap_internal_lock(mh_handle_t heap);
    void          multi_heap_internal_unlock(mh_handle_t heap);
    }

    // ---- tunables --------------------------------------------------------
    // Baseline is ~1100 used blocks (WiFi on).  1600 * 3 B = 4.8 KB .bss.
    // Bump if $HeapSnap reports OVERFLOW; each +256 costs 768 B.
    constexpr uint32_t CAP_BLOCKS               = 1600;
    constexpr uint32_t CAP_ADDED                = 192;  // added blocks captured per diff
    constexpr uint32_t DUMP_BYTES               = 64;   // live content bytes shown per added block
    constexpr uint32_t MAX_PRINT                = 250;  // printed-line cap per category
    constexpr uint32_t HARD_MAX_BLOCKS_PER_HEAP = 200000;
    constexpr uint32_t HARD_MAX_HEAPS           = 32;

    // ESP32 internal DRAM data window (loose).  DRAM_LO is the offset base.
    constexpr uintptr_t DRAM_LO = 0x3FF80000u;
    constexpr uintptr_t DRAM_HI = 0x40000000u;
    inline bool         in_dram(uintptr_t a) {
        return a >= DRAM_LO && a < DRAM_HI;
    }

    // ---- baseline snapshot: sorted 24-bit offsets from DRAM_LO ------------
    uint8_t  g_snapA[CAP_BLOCKS * 3];
    uint8_t  g_seen[(CAP_BLOCKS + 7) / 8];
    uint32_t g_snapA_n     = 0;
    uint32_t g_snapA_over  = 0;
    bool     g_snapA_valid = false;

    // ---- diff results ---------------------------------------------------
    uint32_t g_added_addr[CAP_ADDED];
    uint16_t g_added_size[CAP_ADDED];
    uint32_t g_added_n   = 0;
    uint32_t g_freed_n   = 0;
    uint32_t g_diff_over = 0;

    // ---- walk accounting (cross-check) --------------------------------
    uint32_t g_walk_blocks = 0;
    uint32_t g_walk_heaps  = 0;
    bool     g_layout_ok   = true;

    // ---- $HeapRefs scan target ---------------------------------------
    uint32_t g_ref_lo = 0;
    uint32_t g_ref_hi = 0;

    inline uint32_t rd3(const uint8_t* p) {
        return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16);
    }
    inline void wr3(uint8_t* p, uint32_t v) {
        p[0] = uint8_t(v);
        p[1] = uint8_t(v >> 8);
        p[2] = uint8_t(v >> 16);
    }
    inline void seen_set(uint32_t i) {
        g_seen[i >> 3] |= uint8_t(1u << (i & 7));
    }
    inline bool seen_get(uint32_t i) {
        return (g_seen[i >> 3] >> (i & 7)) & 1u;
    }

    int cmp_off3(const void* a, const void* b) {
        uint32_t x = rd3(static_cast<const uint8_t*>(a));
        uint32_t y = rd3(static_cast<const uint8_t*>(b));
        return (x > y) - (x < y);
    }

    // binary search sorted g_snapA for a 24-bit offset; -1 if absent
    int snapA_find(uint32_t off) {
        int lo = 0;
        int hi = (int)g_snapA_n - 1;
        while (lo <= hi) {
            int      mid = (lo + hi) >> 1;
            uint32_t m   = rd3(&g_snapA[mid * 3]);
            if (m == off) {
                return mid;
            }
            if (m < off) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        return -1;
    }

    bool heap_supports_8bit(const idf_heap_t* h) {
        return ((h->caps[0] | h->caps[1] | h->caps[2]) & MALLOC_CAP_8BIT) != 0;
    }

    // Only validates the SLIST node is safe to keep traversing.  Non-8-bit
    // heaps (e.g. the IRAM heap) are filtered by heap_supports_8bit(), not
    // treated as corruption.
    bool heap_node_sane(const idf_heap_t* h) {
        if (!in_dram((uintptr_t)h) || ((uintptr_t)h & 3)) {
            return false;
        }
        uintptr_t nx = (uintptr_t)h->next;
        if (nx && (!in_dram(nx) || (nx & 3))) {
            return false;
        }
        return h->heap != nullptr;
    }

    // Walk every used MALLOC_CAP_8BIT block, calling cb(addr,size) with the
    // heap spinlock held.  cb MUST NOT allocate, block, or do I/O.
    void walk_used_8bit(void (*cb)(uint32_t addr, uint32_t size)) {
        g_walk_blocks = 0;
        g_walk_heaps  = 0;
        g_layout_ok   = true;

        uint32_t heap_i = 0;
        for (idf_heap_t* h = registered_heaps.slh_first; h && heap_i < HARD_MAX_HEAPS; h = h->next, ++heap_i) {
            if (!heap_node_sane(h)) {
                g_layout_ok = false;
                break;
            }
            if (!heap_supports_8bit(h)) {
                continue;
            }
            ++g_walk_heaps;

            multi_heap_internal_lock(h->heap);
            mh_block_t blk   = multi_heap_get_first_block(h->heap);
            uint32_t   guard = 0;
            while (blk && guard++ < HARD_MAX_BLOCKS_PER_HEAP) {
                void*      a     = multi_heap_get_block_address(blk);
                bool       freeb = multi_heap_is_free(blk);
                mh_block_t nblk  = multi_heap_get_next_block(h->heap, blk);

                uint32_t size = 0;
                if (nblk) {
                    void* na = multi_heap_get_block_address(nblk);
                    if ((uintptr_t)na > (uintptr_t)a) {
                        size = (uint32_t)((uintptr_t)na - (uintptr_t)a);
                    }
                }
                if (!freeb) {
                    ++g_walk_blocks;
                    cb((uint32_t)(uintptr_t)a, size);
                }
                blk = nblk;
            }
            multi_heap_internal_unlock(h->heap);
        }
    }

    // ---- per-block callbacks (spinlock held) --------------------------------

    void cb_snapshot(uint32_t addr, uint32_t /*size*/) {
        uint32_t off = addr - (uint32_t)DRAM_LO;
        if (addr < DRAM_LO || off > 0xFFFFFFu) {
            ++g_snapA_over;
            return;
        }
        if (g_snapA_n < CAP_BLOCKS) {
            wr3(&g_snapA[g_snapA_n * 3], off);
            ++g_snapA_n;
        } else {
            ++g_snapA_over;
        }
    }

    void cb_diff(uint32_t addr, uint32_t size) {
        uint32_t off = addr - (uint32_t)DRAM_LO;
        if (addr < DRAM_LO || off > 0xFFFFFFu) {
            return;
        }
        int idx = snapA_find(off);
        if (idx >= 0) {
            seen_set(idx);
        } else if (g_added_n < CAP_ADDED) {
            g_added_addr[g_added_n] = addr;
            g_added_size[g_added_n] = size > 0xFFFF ? 0xFFFF : (uint16_t)size;
            ++g_added_n;
        } else {
            ++g_diff_over;
        }
    }

    // $HeapRefs: record blocks whose contents hold a pointer into [g_ref_lo,
    // g_ref_hi).  Reuses g_added_* as the result list (addr + first-match
    // offset).  Skips the block that itself owns the target range.
    void cb_refs(uint32_t addr, uint32_t size) {
        if (size < 4) {
            return;
        }
        if (g_ref_lo >= addr && g_ref_lo < addr + size) {
            return;  // this block contains the target -- its internal pointers are noise
        }
        uint32_t        scan = size < 4096 ? size : 4096;
        const uint32_t* w    = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(addr));
        for (uint32_t i = 0, n = scan / 4; i < n; ++i) {
            if (w[i] >= g_ref_lo && w[i] < g_ref_hi) {
                if (g_added_n < CAP_ADDED) {
                    g_added_addr[g_added_n] = addr;
                    g_added_size[g_added_n] = (uint16_t)(i * 4);
                    ++g_added_n;
                } else {
                    ++g_diff_over;
                }
                return;
            }
        }
    }

    // ---- content dump (lock released) ------------------------------------

    void dump_contents(Channel& out, uint32_t addr, uint32_t len) {
        if (!in_dram(addr)) {
            return;
        }
        const uint8_t* p = (const uint8_t*)(uintptr_t)addr;
        for (uint32_t off = 0; off < len; off += 16) {
            char  line[100];
            char* w   = line;
            int   rem = sizeof(line);
            int   n   = snprintf(w, rem, "      %08x  ", (unsigned)(addr + off));
            w += n;
            rem -= n;
            char ascii[17];
            for (uint32_t i = 0; i < 16; ++i) {
                if (off + i < len) {
                    uint8_t c = p[off + i];
                    n         = snprintf(w, rem, "%02x ", c);
                    ascii[i]  = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
                } else {
                    n        = snprintf(w, rem, "   ");
                    ascii[i] = ' ';
                }
                w += n;
                rem -= n;
            }
            ascii[16] = '\0';
            snprintf(w, rem, " |%s|", ascii);
            log_info_to(out, line);
        }
    }

    void hex8(char* buf, uint32_t v) {
        snprintf(buf, 11, "%08x", (unsigned)v);
    }

}  // namespace

Error heap_snapshot(const char* value, AuthenticationLevel, Channel& out) {
    g_snapA_n    = 0;
    g_snapA_over = 0;

    drain_messages();  // flush in-flight LogStream buffers so they are not counted
    walk_used_8bit(cb_snapshot);
    qsort(g_snapA, g_snapA_n, 3, cmp_off3);
    memset(g_seen, 0, (g_snapA_n + 7) / 8);
    g_snapA_valid = true;

    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);
    bool match = (g_walk_blocks == (uint32_t)info.allocated_blocks);

    log_info_to(out,
                "HeapSnap: " << g_snapA_n << " used blocks (" << g_walk_heaps << " heaps)"
                             << (g_snapA_over ? "  OVERFLOW-raise-CAP_BLOCKS" : ""));
    log_info_to(out,
                "  cross-check: walk=" << g_walk_blocks << " vs heap_caps=" << (unsigned)info.allocated_blocks
                                       << (match && g_layout_ok ? "  OK" : "  MISMATCH - heap_t layout suspect"));
    return Error::Ok;
}

Error heap_diff(const char* value, AuthenticationLevel, Channel& out) {
    if (!g_snapA_valid) {
        log_error_to(out, "Run $Heap/Snapshot first");
        return Error::InvalidStatement;
    }

    g_added_n   = 0;
    g_freed_n   = 0;
    g_diff_over = 0;
    memset(g_seen, 0, (g_snapA_n + 7) / 8);

    drain_messages();  // flush in-flight LogStream buffers so they are not counted as "added"
    walk_used_8bit(cb_diff);

    // Count freed first (baseline blocks not seen in the second walk).
    for (uint32_t i = 0; i < g_snapA_n; ++i) {
        if (!seen_get(i)) {
            ++g_freed_n;
        }
    }

    log_info_to(out,
                "HeapDiff: A=" << g_snapA_n << " B=" << g_walk_blocks << "  added=" << g_added_n << " freed=" << g_freed_n
                               << (g_diff_over ? "  ADD-OVERFLOW" : "") << (g_layout_ok ? "" : "  (layout suspect)"));

    char hx[12];

    uint32_t printed = 0;
    for (uint32_t i = 0; i < g_added_n && printed < MAX_PRINT; ++i, ++printed) {
        hex8(hx, g_added_addr[i]);
        log_info_to(out, "  + " << hx << "  " << g_added_size[i] << " B");
        dump_contents(out, g_added_addr[i], g_added_size[i] < DUMP_BYTES ? g_added_size[i] : DUMP_BYTES);
    }

    printed = 0;
    for (uint32_t i = 0; i < g_snapA_n && printed < MAX_PRINT; ++i) {
        if (!seen_get(i)) {
            hex8(hx, (uint32_t)DRAM_LO + rd3(&g_snapA[i * 3]));
            log_info_to(out, "  - " << hx);
            ++printed;
        }
    }

    return Error::Ok;
}

Error heap_refs(const char* value, AuthenticationLevel, Channel& out) {
    if (!value || !*value) {
        log_error_to(out, "usage: $Heap/Refs=<hexaddr>[,span]");
        return Error::InvalidStatement;
    }
    char*    end    = nullptr;
    uint32_t target = (uint32_t)strtoul(value, &end, 16);
    uint32_t span   = 4;
    while (end && (*end == ' ' || *end == ',' || *end == '\t')) {
        ++end;
    }
    if (end && *end) {
        uint32_t s = (uint32_t)strtoul(end, nullptr, 0);
        if (s >= 4) {
            span = s;
        }
    }
    g_ref_lo    = target;
    g_ref_hi    = target + span;
    g_added_n   = 0;
    g_diff_over = 0;

    drain_messages();  // flush in-flight LogStream buffers so they are not counted
    walk_used_8bit(cb_refs);

    char lo[12];
    hex8(lo, target);
    log_info_to(out,
                "HeapRefs: " << g_added_n << " blocks point into [" << lo << " +" << span << "]  (B=" << g_walk_blocks << ")"
                             << (g_diff_over ? "  OVERFLOW" : "") << (g_layout_ok ? "" : "  (layout suspect)"));

    char     hx[12];
    uint32_t printed = 0;
    for (uint32_t i = 0; i < g_added_n && printed < MAX_PRINT; ++i, ++printed) {
        hex8(hx, g_added_addr[i]);
        log_info_to(out, "  @ " << hx << "  +" << g_added_size[i]);
        dump_contents(out, g_added_addr[i], DUMP_BYTES);
    }
    return Error::Ok;
}

#endif  // HEAPDIFF
