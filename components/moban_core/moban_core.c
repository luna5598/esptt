#include "moban_core.h"
#include <string.h>

static uint64_t current_boot;
static mo_slot_view slots[MO_SLOT_COUNT];
static mo_course_view course;
static bool holding, latched;
static uint64_t near_since;
enum { NEAR_MIN_MM = 60, NEAR_MAX_MM = 120, REARM_MM = 180, HOLD_MS = 500 };

void mo_init(uint64_t boot_id)
{
    holding = latched = false;
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

uint64_t mo_elapsed_ms(uint64_t now)
{
    if (course.tip == MO_RANGEFINDER && course.phase == MO_RUNNING &&
        !course.timer_paused && now >= course.timer_since_ms)
        return course.elapsed_ms + (now - course.timer_since_ms);
    return course.elapsed_ms;
}

int mo_presence(uint32_t slot, uint32_t present)
{
    if (present > 1) return MO_RANGE;
    const mo_slot_view *view = mo_slot(slot);
    return mo_set_tip(slot, present ? MO_THERMOMETER : MO_NO_TIP, view ? view->sampled_ms : 0);
}

int mo_set_tip(uint32_t slot, enum mo_tip tip, uint64_t now)
{
    if (slot >= MO_SLOT_COUNT) return MO_BAD_SLOT;
    if (tip != MO_NO_TIP && tip != MO_THERMOMETER && tip != MO_RANGEFINDER) return MO_RANGE;
    mo_slot_view *s = &slots[slot];
    if (s->tip == (uint32_t)tip) return MO_OK;
    if (s->generation == UINT32_MAX) return MO_RANGE;
    uint32_t next = s->generation + 1;
    *s = (mo_slot_view){ .generation = next, .tip = tip, .present = tip != MO_NO_TIP };
    if (course.phase == MO_RUNNING && course.slot == slot) {
        course.elapsed_ms = mo_elapsed_ms(now);
        course.phase = MO_WAITING;
        holding = latched = false;
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
    if (slots[slot].tip != MO_THERMOMETER) return MO_WRONG_TIP;
    if (humidity > 10000 || temp < -4000 || temp > 8500) return MO_RANGE;
    mo_slot_view *s = &slots[slot];
    if (s->has_sample && now <= s->sampled_ms) return MO_NO_FRESH_DATA;
    s->has_sample = 1;
    s->temp_centi = temp;
    s->humidity_centi = humidity;
    s->sampled_ms = now;
    s->sample_count++;
    if (course.phase == MO_RUNNING && course.slot == slot &&
        course.generation == generation && course.tip == MO_THERMOMETER) {
        uint32_t n = course.count;
        if (n > 0 && (now <= course.time_ms[n - 1] || now - course.time_ms[n - 1] < 1000u)) return MO_OK;
        course.temp_centi[n] = temp;
        course.time_ms[n] = now;
        course.count++;
        if (course.count == MO_GOAL) course.phase = MO_DONE;
    }
    return MO_OK;
}

int mo_distance_sample(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t now,
                       int32_t mm)
{
    int rc = check_target(boot, slot, generation);
    if (rc != MO_OK) return rc;
    mo_slot_view *s = &slots[slot];
    if (s->tip != MO_RANGEFINDER) return MO_WRONG_TIP;
    if (mm <= 0 || mm > INT16_MAX) return MO_RANGE;
    if (s->sample_count && now <= s->sampled_ms) return MO_NO_FRESH_DATA;
    bool consecutive = s->has_sample && now - s->sampled_ms <= MO_DISTANCE_FRESH_MS;
    s->has_sample = 1;
    s->distance_mm = mm;
    s->sampled_ms = now;
    s->sample_count++;
    if (course.phase != MO_RUNNING || course.tip != MO_RANGEFINDER || course.slot != slot ||
        course.generation != generation || now < course.timer_since_ms) return MO_OK;
    if (!consecutive || mm < NEAR_MIN_MM || mm > NEAR_MAX_MM) holding = false;
    if (mm > REARM_MM) latched = false;
    if (latched || mm < NEAR_MIN_MM || mm > NEAR_MAX_MM) return MO_OK;
    if (!holding) {
        near_since = now;
        holding = true;
    }
    if (now - near_since < HOLD_MS) return MO_OK;
    holding = false;
    latched = true;
    course.elapsed_ms = mo_elapsed_ms(now);
    course.timer_paused = !course.timer_paused;
    course.timer_since_ms = now;
    if (course.count == MO_GOAL) {
        memmove(course.distance_mm, course.distance_mm + 1, (MO_GOAL-1) * sizeof(course.distance_mm[0]));
        memmove(course.time_ms, course.time_ms + 1, (MO_GOAL-1) * sizeof(course.time_ms[0]));
        course.count--;
    }
    course.distance_mm[course.count] = mm;
    course.time_ms[course.count++] = now;
    if (course.events < UINT32_MAX) course.events++;
    return MO_OK;
}

void mo_invalidate(uint32_t slot)
{
    if (slot < MO_SLOT_COUNT) slots[slot].has_sample = 0;
    if (course.slot == slot) holding = false;
}

bool mo_sample_fresh(const mo_slot_view *slot, uint64_t now)
{
    return slot->present && slot->has_sample && now >= slot->sampled_ms &&
           now - slot->sampled_ms <= (slot->tip == MO_RANGEFINDER ? MO_DISTANCE_FRESH_MS : MO_FRESH_MS);
}

int mo_read(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t now)
{
    int rc = check_target(boot, slot, generation);
    if (rc != MO_OK) return rc;
    const mo_slot_view *s = &slots[slot];
    if (!mo_sample_fresh(s, now)) return MO_NO_FRESH_DATA;
    return MO_OK;
}

enum mo_action mo_button_action(enum mo_button key, uint32_t slot)
{
    const mo_slot_view *s = mo_slot(slot);
    switch (key) {
    case MO_KEY_START:
        if (!s || !s->present) return 0;
        return s->tip == MO_RANGEFINDER ? MO_START_DISTANCE : MO_START_TEMP;
    case MO_KEY_RESUME: return MO_RESUME_COURSE;
    case MO_KEY_STOP: return MO_STOP_COURSE;
    default: return 0;
    }
}

enum mo_action mo_phrase_action(const char *phrase)
{
    if (!phrase) return 0;
    if (!strcmp(phrase, "开始测距")) return MO_START_DISTANCE;
    if (!strcmp(phrase, "开始测温")) return MO_START_TEMP;
    if (!strcmp(phrase, "继续记录")) return MO_RESUME_COURSE;
    if (!strcmp(phrase, "停止记录")) return MO_STOP_COURSE;
    return 0;
}

int mo_start(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t now, uint32_t expected_run)
{
    return mo_dispatch(MO_START_TEMP, boot, slot, generation, now, expected_run);
}

int mo_dispatch(enum mo_action action, uint64_t boot, uint32_t slot, uint32_t generation,
                uint64_t now, uint32_t expected_run)
{
    if (action == MO_RESUME_COURSE) return mo_resume(boot, slot, generation, now, expected_run);
    if (action == MO_STOP_COURSE) {
        if (boot != current_boot) return MO_OLD_BOOT;
        if (slot >= MO_SLOT_COUNT) return MO_BAD_SLOT;
        if (slots[slot].generation != generation) return MO_OLD_TARGET;
        if (course.run_id != expected_run) return MO_COURSE_CHANGED;
        if (slot != course.slot || (course.phase != MO_RUNNING && course.phase != MO_WAITING))
            return MO_WRONG_PHASE;
        course.elapsed_ms = mo_elapsed_ms(now);
        course.phase = MO_STOPPED;
        return MO_OK;
    }
    if (action != MO_START_TEMP && action != MO_START_DISTANCE) return MO_RANGE;
    int rc = mo_read(boot, slot, generation, now);
    if (rc != MO_OK) return rc;
    if (course.run_id != expected_run) return MO_COURSE_CHANGED;
    if (course.phase == MO_RUNNING || course.phase == MO_WAITING) return MO_WRONG_PHASE;
    enum mo_tip tip = action == MO_START_TEMP ? MO_THERMOMETER : MO_RANGEFINDER;
    if (slots[slot].tip != (uint32_t)tip) return MO_WRONG_TIP;
    holding = latched = false;
    if (expected_run == UINT32_MAX) return MO_RANGE;
    course = (mo_course_view){ .run_id = expected_run + 1, .phase = MO_RUNNING,
                               .slot = slot, .generation = generation, .tip = tip, .timer_since_ms = now };
    return MO_OK;
}

int mo_resume(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t now,
              uint32_t expected_run)
{
    int rc = mo_read(boot, slot, generation, now);
    if (rc != MO_OK) return rc;
    if (course.run_id != expected_run) return MO_COURSE_CHANGED;
    if (course.phase != MO_WAITING || slot != course.slot) return MO_WRONG_PHASE;
    if (slots[slot].tip != course.tip) return MO_WRONG_TIP;
    course.timer_since_ms = now;
    course.generation = generation;
    course.phase = MO_RUNNING;
    return MO_OK;
}
