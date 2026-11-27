#undef NDEBUG
#include <assert.h>
#include "slot_observation.h"

int main(void)
{
    mo_init(17);
    limb_poll_result poll = {.result = LIMB_NEW_SAMPLE,
        .sample = {.type = LIMB_DISTANCE, .a = 90, .age_ms = MO_DISTANCE_FRESH_MS}};
    mo_observe_slot(17, 0, 1000, &poll);
    const mo_slot_view *s = mo_slot(0);
    assert(s->tip == MO_RANGEFINDER && s->sampled_ms == 750 && mo_sample_fresh(s, 1000));
    uint32_t gen = s->generation;
    poll.result = LIMB_UNCHANGED;
    mo_observe_slot(17, 0, 1001, &poll);
    assert(s->sample_count == 1 && !mo_sample_fresh(s, 1001));

    poll.result = LIMB_NEW_SAMPLE;
    poll.sample.age_ms++;
    mo_observe_slot(17, 0, 1200, &poll);
    assert(!s->has_sample && s->generation == gen);
    poll.sample.type = 254;
    mo_observe_slot(17, 0, 1201, &poll);
    assert(!s->present && s->tip == MO_NO_TIP);

    poll.sample.type = LIMB_TEMP_HUM;
    poll.sample.age_ms = MO_FRESH_MS;
    mo_observe_slot(17, 0, 2000, &poll);
    assert(s->tip == MO_THERMOMETER && !s->has_sample); /* Earlier than boot. */
    mo_observe_slot(17, 0, 3000, &poll);
    assert(s->sampled_ms == 500 && mo_sample_fresh(s, 3000));
    return 0;
}
