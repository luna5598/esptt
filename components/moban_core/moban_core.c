#include "moban_core.h"
#include <string.h>

static uint64_t current_boot;
static mo_slot_view slots[MO_SLOT_COUNT];
static mo_course_view course;

void mo_init(uint64_t boot_id)
{
    current_boot = boot_id;
    memset(slots, 0, sizeof(slots));
    memset(&course, 0, sizeof(course));
}

const mo_slot_view *mo_slot(uint32_t slot)
{
    return slot < MO_SLOT_COUNT ? &slots[slot] : NULL;
}

const mo_course_view *mo_course(void)
{
    return &course;
}

int mo_presence(uint32_t slot, uint32_t present)
{
    if (slot >= MO_SLOT_COUNT) return MO_BAD_SLOT;
    if (present > 1) return MO_RANGE;
    mo_slot_view *s = &slots[slot];
    if (s->present == present) return MO_OK;
    if (s->generation == UINT32_MAX) return MO_RANGE;
    uint32_t next = s->generation + 1;
    *s = (mo_slot_view){ .generation = next, .present = present };
    if (course.phase == MO_RUNNING && course.slot == slot) {
        course.phase = MO_WAITING;
    }
    return MO_OK;
}

static int check_target(uint64_t boot, uint32_t slot, uint32_t generation)
{
    if (boot != current_boot) return MO_OLD_BOOT;
    if (slot >= MO_SLOT_COUNT) return MO_BAD_SLOT;
    if (slots[slot].generation != generation) return MO_OLD_TARGET;
    return slots[slot].present ? MO_OK : MO_EMPTY;
}

int mo_sample(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t now,
              int32_t temp, uint32_t humidity)
{
    int rc = check_target(boot, slot, generation);
    if (rc != MO_OK) return rc;
    if (humidity > 10000 || temp < -4000 || temp > 8500) return MO_RANGE;
    mo_slot_view *s = &slots[slot];
    if (s->has_sample && now <= s->sampled_ms) return MO_NO_FRESH_DATA;
    s->has_sample = 1;
    s->temp_centi = temp;
    s->humidity_centi = humidity;
    s->sampled_ms = now;
    s->sample_count++;
    if (course.phase == MO_RUNNING && course.slot == slot &&
        course.generation == generation) {
        uint32_t n = course.count;
        if (n > 0 && (now <= course.time_ms[n - 1] || now - course.time_ms[n - 1] < 1000u)) return MO_OK;
        course.temp_centi[n] = temp;
        course.time_ms[n] = now;
        course.count++;
        if (course.count == MO_GOAL) course.phase = MO_DONE;
    }
    return MO_OK;
}

void mo_invalidate(uint32_t slot)
{
    if (slot < MO_SLOT_COUNT) slots[slot].has_sample = 0;
}

bool mo_sample_fresh(const mo_slot_view *slot, uint64_t now)
{
    return slot->present && slot->has_sample && now >= slot->sampled_ms &&
           now - slot->sampled_ms <= MO_FRESH_MS;
}

int mo_read(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t now)
{
    int rc = check_target(boot, slot, generation);
    if (rc != MO_OK) return rc;
    const mo_slot_view *s = &slots[slot];
    if (!mo_sample_fresh(s, now)) return MO_NO_FRESH_DATA;
    return MO_OK;
}

int mo_start(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t now,
             uint32_t expected_run)
{
    int rc = mo_read(boot, slot, generation, now);
    if (rc != MO_OK) return rc;
    if (course.run_id != expected_run) return MO_COURSE_CHANGED;
    if (course.phase == MO_RUNNING || course.phase == MO_WAITING) return MO_WRONG_PHASE;
    if (expected_run == UINT32_MAX) return MO_RANGE;
    course = (mo_course_view){ .run_id = expected_run + 1, .phase = MO_RUNNING,
                               .slot = slot, .generation = generation };
    return MO_OK;
}

int mo_resume(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t now,
              uint32_t expected_run)
{
    int rc = mo_read(boot, slot, generation, now);
    if (rc != MO_OK) return rc;
    if (course.run_id != expected_run) return MO_COURSE_CHANGED;
    if (course.phase != MO_WAITING || slot != course.slot) return MO_WRONG_PHASE;
    course.generation = generation;
    course.phase = MO_RUNNING;
    return MO_OK;
}
