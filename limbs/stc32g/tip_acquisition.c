#include "tip_acquisition.h"
#include <string.h>

enum {
    UNKNOWN = 255, ID_VOTES = 3, ID_SPACING_MS = 10, ID_MAX_GAP_MS = 50,
    AHT_ADDRESS = 0x38, POWER_ON_MS = 100, COMMAND_WAIT_MS = 10,
    CONVERSION_MS = 80, TIMEOUT_MS = 250, PERIOD_MS = 1000
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
    if (s->phase != AHT_OFF) s->io.power(s->io.ctx, false);
    s->phase = AHT_OFF;
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
    if (type == LIMB_TEMP_HUM) {
        s->node.probing = true;
        s->init_sent = false;
        s->phase = AHT_PROBE;
        s->phase_start = s->last_io = now;
        s->io.power(s->io.ctx, true);
    }
}

static void retry(tip_acquisition *s, uint32_t now)
{
    s->node.sample_flags = 0;
    s->phase = AHT_RETRY;
    s->phase_start = now;
}

static void sample(tip_acquisition *s, uint32_t now)
{
    static const uint8_t init[] = {0xbe, 0x08, 0x00};
    static const uint8_t trigger[] = {0xac, 0x33, 0x00};
    uint32_t elapsed = now - s->phase_start;
    uint8_t data[7];

    switch (s->phase) {
    case AHT_PROBE:
        if (elapsed < POWER_ON_MS || now - s->last_io < COMMAND_WAIT_MS) return;
        /* Reading one byte at 7-bit 0x38 sends the 0x71 read address. */
        if (!s->io.read(s->io.ctx, AHT_ADDRESS, data, 1)) break;
        s->last_io = now;
        if (!(data[0] & 0x80u)) {
            if (data[0] & 0x08u) {
                s->node.probing = false;
                s->phase = AHT_TRIGGER;
                s->phase_start = now - PERIOD_MS;
                return;
            }
            if (!s->init_sent) {
                if (!s->io.write(s->io.ctx, AHT_ADDRESS, init, sizeof(init))) break;
                s->init_sent = true;
            }
        }
        if (elapsed < POWER_ON_MS + TIMEOUT_MS) return;
        break;
    case AHT_TRIGGER:
        if (elapsed < PERIOD_MS) return;
        if (!s->io.write(s->io.ctx, AHT_ADDRESS, trigger, sizeof(trigger))) break;
        s->conversion_gen = s->node.generation;
        s->phase_start = s->last_io = now;
        s->phase = AHT_CONVERTING;
        return;
    case AHT_CONVERTING:
        if (elapsed < CONVERSION_MS || now - s->last_io < COMMAND_WAIT_MS) return;
        if (elapsed >= TIMEOUT_MS || !s->io.read(s->io.ctx, AHT_ADDRESS, data, sizeof(data))) break;
        s->last_io = now;
        if (data[0] & 0x80u) return;
        if (!temperature_node_receive_aht20(&s->node, s->conversion_gen, data, sizeof(data)) &&
            s->node.sample_flags != 2) break;
        /* Timestamp the trigger so a late read never makes old data look fresh. */
        s->node.age_ms = (uint16_t)elapsed;
        s->phase = AHT_TRIGGER;
        s->phase_start = now;
        return;
    case AHT_RETRY:
        if (elapsed >= PERIOD_MS) {
            s->phase = AHT_PROBE;
            s->phase_start = now - POWER_ON_MS;
            s->last_io = now - COMMAND_WAIT_MS;
            s->init_sent = false;
        }
        return;
    case AHT_OFF:
        return;
    }
    retry(s, now);
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
    sample(s, now);
}
