#include "tip_acquisition.h"
#include <string.h>

enum {
    UNKNOWN = 255, ID_VOTES = 3, ID_SPACING_MS = 10, ID_MAX_GAP_MS = 50
};

bool tip_acquisition_init(tip_acquisition *s, tip_id_profile profile, tip_io io)
{
    size_t i;
    memset(s, 0, sizeof(*s));
    if (!io.power) return false;
    io.power(io.ctx, false);
    if (!io.read || !io.write || !profile.bands || !profile.count ||
        profile.open_min > profile.adc_max) return false;
    for (i = 0; i < profile.count; i++) {
        const tip_id_band *b = &profile.bands[i];
        if (b->type == LIMB_DISTANCE && !io.read_reg16) return false;
        if (!b->type || b->type == UNKNOWN || b->low > b->high || b->high >= profile.open_min ||
            (i && b->low <= profile.bands[i-1].high)) return false;
    }
    s->profile = profile;
    s->io = io;
    return true;
}

static uint8_t classify(const tip_id_profile *profile, uint16_t adc)
{
    size_t i;
    if (adc > profile->adc_max) return UNKNOWN;
    if (adc >= profile->open_min) return 0;
    for (i = 0; i < profile->count; i++) {
        const tip_id_band *b = &profile->bands[i];
        if (adc >= b->low && adc <= b->high) return b->type;
    }
    return UNKNOWN;
}

static void retire(tip_acquisition *s)
{
    if (limb_sensor_cap(s->node.type)) s->io.power(s->io.ctx, false);
    s->votes = 0;
    temperature_node_set_tip(&s->node, 0);
}

static void qualify(tip_acquisition *s, uint8_t type, uint32_t now)
{
    uint32_t gap = now - s->last_vote;
    if (s->candidate != type || gap > ID_MAX_GAP_MS) s->votes = 0;
    if (s->votes && gap < ID_SPACING_MS) return;
    s->candidate = type;
    s->last_vote = now;
    if (++s->votes < ID_VOTES) return;
    s->votes = 0;
    temperature_node_set_tip(&s->node, type);
    if (limb_sensor_cap(type)) {
        s->node.probing = true;
        s->io.power(s->io.ctx, true);
    }
    if (type == LIMB_TEMP_HUM) aht20_start(&s->sensor.aht, now);
    if (type == LIMB_DISTANCE) vl53l1x_start(&s->sensor.range, now, s->node.generation);
}

void tip_acquisition_tick(tip_acquisition *s, uint32_t now, bool seated,
                          bool detached, uint16_t adc)
{
    uint8_t type;
    if (!s->io.power) return;
    temperature_node_advance(&s->node, now - s->last_tick);
    s->last_tick = now;
    type = seated ? classify(&s->profile, adc) : 0;
    if (detached || !type || (s->node.type && type != s->node.type)) retire(s);
    if (!type) return;
    if (s->node.type != type) {
        qualify(s, type, now);
        return;
    }
    switch (type) {
    case LIMB_TEMP_HUM: aht20_tick(&s->sensor.aht, &s->io, &s->node, now); break;
    case LIMB_DISTANCE: vl53l1x_tick(&s->sensor.range, &s->io, &s->node, now); break;
    }
}
