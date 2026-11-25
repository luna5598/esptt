#ifndef MOBAN_CORE_H
#define MOBAN_CORE_H

#include <stdint.h>

#define MO_SLOT_COUNT 4u
#define MO_GOAL 10u
#define MO_FRESH_MS 2500u
enum mo_phase { MO_IDLE, MO_RUNNING, MO_WAITING, MO_DONE };
enum mo_result { MO_OK, MO_BAD_SLOT, MO_OLD_BOOT, MO_OLD_TARGET, MO_EMPTY,
                 MO_NO_FRESH_DATA, MO_COURSE_CHANGED, MO_WRONG_PHASE, MO_RANGE };

/* Single device instance. Serialize calls and copy view data before releasing
 * the caller's lock. Times are monotonic milliseconds. */
typedef struct {
    uint32_t generation, present, has_sample;
    int32_t temp_centi;
    uint32_t humidity_centi, sample_count;
    uint64_t sampled_ms;
} mo_slot_view;

typedef struct {
    uint32_t run_id, phase, slot, generation, count;
    int32_t temp_centi[MO_GOAL];
    uint64_t time_ms[MO_GOAL];
} mo_course_view;

void mo_init(uint64_t boot_id);
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
