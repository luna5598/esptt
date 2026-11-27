#ifndef MOBAN_CORE_H
#define MOBAN_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MO_SLOT_COUNT 4u
#define MO_GOAL 10u
#define MO_FRESH_MS 2500u
enum mo_phase { MO_IDLE, MO_RUNNING, MO_WAITING, MO_DONE, MO_STOPPED };
enum mo_result { MO_OK, MO_BAD_SLOT, MO_OLD_BOOT, MO_OLD_TARGET, MO_EMPTY,
                 MO_NO_FRESH_DATA, MO_COURSE_CHANGED, MO_WRONG_PHASE, MO_RANGE, MO_WRONG_TIP };
enum mo_tip { MO_NO_TIP, MO_THERMOMETER, MO_RANGEFINDER };
enum mo_action { MO_START_TEMP = 1, MO_START_DISTANCE, MO_RESUME_COURSE, MO_STOP_COURSE };
enum mo_button { MO_KEY_START = 1, MO_KEY_RESUME, MO_KEY_STOP };
#define MO_DISTANCE_FRESH_MS 250u

/* Single device instance. Serialize calls and copy view data before releasing
 * the caller's lock. Times are monotonic milliseconds. */
typedef struct {
    uint32_t generation, present, has_sample;
    int32_t temp_centi;
    uint32_t humidity_centi, sample_count;
    uint64_t sampled_ms;
    uint32_t tip;
    int32_t distance_mm;
} mo_slot_view;

typedef struct {
    uint32_t run_id, phase, slot, generation, count;
    union { int32_t temp_centi[MO_GOAL]; int32_t distance_mm[MO_GOAL]; };
    uint64_t time_ms[MO_GOAL];
    uint32_t tip, events, timer_paused;
    uint64_t elapsed_ms, timer_since_ms;
} mo_course_view;

bool mo_sample_fresh(const mo_slot_view *slot, uint64_t now_ms);
void mo_init(uint64_t boot_id);
int mo_set_tip(uint32_t slot, enum mo_tip tip, uint64_t now_ms);
int mo_distance_sample(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t sampled_ms,
                       int32_t distance_mm);
uint64_t mo_elapsed_ms(uint64_t now_ms);
typedef struct {
    enum mo_action action;
    const char *text;
    const char *pinyin;
} mo_spoken_command;

const mo_spoken_command *mo_spoken_commands(size_t *count);
enum mo_action mo_phrase_action(const char *phrase);
enum mo_action mo_button_action(enum mo_button key, uint32_t slot);
int mo_dispatch(enum mo_action action, uint64_t boot, uint32_t slot, uint32_t generation,
                uint64_t now_ms, uint32_t expected_run);
int mo_presence(uint32_t slot, uint32_t present);
int mo_sample(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t now_ms,
              int32_t temp_centi, uint32_t humidity_centi);
void mo_invalidate(uint32_t slot);
int mo_read(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t now_ms);
int mo_start(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t now_ms,
             uint32_t expected_run);
int mo_resume(uint64_t boot, uint32_t slot, uint32_t generation, uint64_t now_ms,
              uint32_t expected_run);
const mo_slot_view *mo_slot(uint32_t slot);
const mo_course_view *mo_course(void);

#endif
