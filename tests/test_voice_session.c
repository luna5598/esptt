#undef NDEBUG
#include <assert.h>
#include <string.h>
#include "mo_voice_session.h"

int main(void)
{
    mo_slot_view slots[MO_SLOT_COUNT] = {0};
    mo_course_view course = {.slot = 0};
    slots[0].generation = 4;
    slots[2] = (mo_slot_view){.present = 1, .tip = MO_THERMOMETER, .generation = 1};
    slots[3] = (mo_slot_view){.present = 1, .tip = MO_RANGEFINDER, .generation = 7};
    mo_voice_session wake;
    mo_voice_arm(&wake, 17, 100, slots, &course, 0);
    mo_voice_session session = wake;
    enum mo_action action;
    mo_voice_target target;
    size_t count;
    const mo_spoken_command *commands = mo_spoken_commands(&count);
    assert(count == 4);
    for (size_t i = 0; i < count; ++i) {
        session = wake;
        assert(mo_phrase_action(commands[i].text) == commands[i].action);
        assert(commands[i].pinyin[0] && mo_voice_take(&session, commands[i].action, 110, &action, &target));
        assert(action == commands[i].action);
        if (action == MO_START_TEMP) assert(target.slot == 2 && target.generation == 1);
        else if (action == MO_START_DISTANCE) assert(target.slot == 3 && target.generation == 7);
        else assert(target.slot == 0 && target.generation == 4);
        assert(!mo_voice_take(&session, action, 110, &action, &target));
        for (size_t j = 0; j < i; ++j) {
            assert(commands[j].action != action);
            assert(strcmp(commands[j].pinyin, commands[i].pinyin));
        }
    }

    session = wake;
    assert(!mo_voice_take(&session, MO_START_TEMP, 5100, &action, &target));
    session = wake;
    assert(!mo_voice_take(&session, MO_START_TEMP, 99, &action, &target));
    session = wake;
    assert(!mo_voice_take(&session, 999, 101, &action, &target));

    slots[1] = slots[2];
    mo_voice_arm(&session, 17, 100, slots, &course, 0);
    assert(!mo_voice_take(&session, MO_START_TEMP, 110, &action, &target));
    mo_voice_arm(&session, 17, 100, slots, &course, 1);
    assert(mo_voice_take(&session, MO_START_TEMP, 110, &action, &target) && target.slot == 1);
    slots[3].present = 0;
    mo_voice_arm(&session, 17, 100, slots, &course, 1);
    assert(!mo_voice_take(&session, MO_START_DISTANCE, 110, &action, &target));

    mo_init(wake.boot);
    mo_set_tip(2, MO_THERMOMETER, 100);
    mo_set_tip(2, MO_NO_TIP, 102);
    mo_set_tip(2, MO_THERMOMETER, 103);
    mo_sample(wake.boot, 2, 3, 104, 2200, 6000);
    session = wake;
    assert(mo_voice_take(&session, MO_START_TEMP, 110, &action, &target));
    assert(mo_dispatch(action, session.boot, target.slot, target.generation, 110, session.run) == MO_OLD_TARGET);
    session = wake;
    session.temperature.generation = 3;
    assert(mo_dispatch(MO_START_TEMP, wake.boot, 2, 3, 111, 0) == MO_OK);
    assert(mo_voice_take(&session, MO_START_TEMP, 112, &action, &target));
    assert(mo_dispatch(action, session.boot, target.slot, target.generation, 112, session.run) == MO_COURSE_CHANGED);
    return 0;
}
