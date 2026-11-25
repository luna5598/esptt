#pragma once

#include "limb_reader.h"
#include "moban_core.h"

typedef struct {
    uint64_t boot, now;
    mo_slot_view slots[MO_SLOT_COUNT];
    enum limb_result status[MO_SLOT_COUNT];
    mo_course_view course;
    bool bus_ready;
} device_snapshot;

void device_init(void);
void device_bus_ready(void);
void device_observe(unsigned slot, const limb_poll_result *poll);
void device_snapshot_read(device_snapshot *out);
int device_course_action(bool resume, uint64_t boot, unsigned slot, uint32_t gen, uint32_t run);
