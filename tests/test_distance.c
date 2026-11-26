#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include "moban_core.h"

static const uint64_t boot = 42;
static const unsigned slot = 2;

static void reading(uint64_t ms, int mm)
{
    assert(mo_distance_sample(boot, slot, mo_slot(slot)->generation, ms, mm) == MO_OK);
}

int main(void)
{
    mo_init(boot);
    assert(mo_set_tip(slot, MO_RANGEFINDER, 0) == MO_OK);
    reading(10, 280);
    assert(mo_read(boot,slot,1,260) == MO_OK && mo_read(boot,slot,1,261) == MO_NO_FRESH_DATA);
    assert(mo_start(boot, slot, 1, 100, 0) == MO_WRONG_TIP);
    assert(mo_phrase_action("开始测距") == mo_button_action(MO_KEY_START,slot));
    assert(!mo_phrase_action("未知指令") && !mo_phrase_action(NULL));
    assert(mo_dispatch(MO_START_DISTANCE, boot, slot, 1, 100, 0) == MO_OK);

    reading(200, 60);
    reading(400, 120);
    mo_invalidate(slot);
    reading(600, 100);
    reading(800, 100);
    assert(mo_course()->events == 0);
    reading(1100, 100); /* A missing interval interrupts the hold. */
    reading(1300, 100);
    reading(1500, 100);
    reading(1599, 100);
    assert(mo_course()->events == 0);
    reading(1600, 100);
    assert(mo_course()->events == 1 && mo_course()->timer_paused);
    assert(mo_elapsed_ms(9999) == 1500);
    reading(1800, 180);
    reading(2000, 120);
    reading(2200, 90);
    reading(2400, 60);
    reading(2600, 100);
    assert(mo_course()->events == 1);
    reading(2700, 181);
    reading(2800, 100);
    reading(3000, 100);
    reading(3200, 100);
    reading(3300, 100);
    assert(mo_course()->events == 2 && !mo_course()->timer_paused);
    assert(mo_elapsed_ms(3400) == 1600);
    assert(mo_set_tip(slot, MO_NO_TIP, 3500) == MO_OK);
    assert(mo_elapsed_ms(4000) == 1700 && mo_course()->phase == MO_WAITING);
    mo_set_tip(slot, MO_THERMOMETER, 4000);
    mo_sample(boot, slot, 3, 4000, 0, 0);
    assert(mo_resume(boot, slot, 3, 4000, 1) == MO_WRONG_TIP);
    assert(mo_dispatch(MO_STOP_COURSE, boot, slot, 1, 4000, 1) == MO_OLD_TARGET);
    mo_set_tip(slot, MO_RANGEFINDER, 4100);
    reading(4100, 100);
    assert(mo_resume(boot, slot, 4, 4100, 1) == MO_OK);
    assert(mo_distance_sample(boot, slot, 4, 4100, 100) == MO_NO_FRESH_DATA);
    assert(mo_elapsed_ms(4300) == 1900 && mo_course()->events == 2);
    assert(mo_dispatch(MO_STOP_COURSE, boot, slot, 4, 4300, 1) == MO_OK);
    assert(mo_elapsed_ms(9999) == 1900 && mo_course()->phase == MO_STOPPED);
    reading(4400, 100);
    assert(mo_dispatch(MO_START_DISTANCE, boot, slot, 4, 4400, 1) == MO_OK);
    assert(mo_dispatch(MO_STOP_COURSE, boot, slot, 4, 4500, 1) == MO_COURSE_CHANGED);
    uint64_t now = 4400;
    for (unsigned i = 0; i < MO_GOAL + 2; i++) {
        reading(now += 100, 200);
        for (unsigned j = 0; j < 6; j++) reading(now += 100, 70 + (int)i);
    }
    assert(mo_course()->count == MO_GOAL && mo_course()->events == MO_GOAL + 2);
    assert(mo_course()->distance_mm[0] == 72 && mo_course()->distance_mm[MO_GOAL-1] == 81);
    mo_set_tip(slot, MO_NO_TIP, now);
    assert(mo_dispatch(MO_STOP_COURSE, boot, slot, 5, now, 2) == MO_OK);
    return 0;
}
