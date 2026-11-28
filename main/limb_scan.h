#pragma once

#include "limb_reader.h"
#include "moban_core.h"

/* One polling owner drives both the S31 and desktop versions of this policy.
 * recover must use the independent reset pin and verify the isolated mux.
 * poll returns false for transport errors; node-reported faults live in out. */
typedef struct {
    void *ctx;
    bool (*select)(void *ctx, unsigned slot);
    bool (*recover)(void *ctx);
    bool (*poll)(void *ctx, unsigned slot, limb_poll_result *out);
} limb_scan_io;

typedef struct {
    uint64_t slot_retry_ms[MO_SLOT_COUNT];
    uint64_t bus_retry_ms;
} limb_scan;

enum scan_event { SCAN_SKIP, SCAN_OBSERVED, SCAN_BUS_DOWN };

enum scan_event limb_scan_slot(limb_scan *scan, const limb_scan_io *io, unsigned slot,
                               uint64_t now, limb_poll_result *out);
