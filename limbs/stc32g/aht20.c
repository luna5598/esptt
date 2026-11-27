#include "aht20.h"

enum {
    AHT_ADDRESS = 0x38, POWER_ON_MS = 100, COMMAND_WAIT_MS = 10,
    CONVERSION_MS = 80, TIMEOUT_MS = 250, PERIOD_MS = 1000
};

void aht20_start(aht20 *s, uint32_t now)
{
    s->phase = AHT_PROBE;
    s->phase_start = s->last_io = now;
    s->init_sent = false;
}

static void retry(aht20 *s, temperature_node *node, uint32_t now)
{
    node->sample_flags = 0;
    s->phase = AHT_RETRY;
    s->phase_start = now;
}

void aht20_tick(aht20 *s, const tip_io *io, temperature_node *node, uint32_t now)
{
    static const uint8_t init[] = {0xbe, 0x08, 0x00};
    static const uint8_t trigger[] = {0xac, 0x33, 0x00};
    uint32_t elapsed = now - s->phase_start;
    uint8_t payload[7];

    switch (s->phase) {
    case AHT_PROBE:
        if (elapsed < POWER_ON_MS || now - s->last_io < COMMAND_WAIT_MS) return;
        /* Reading one byte at 7-bit 0x38 sends the 0x71 read address. */
        if (!io->read(io->ctx, AHT_ADDRESS, payload, 1)) break;
        s->last_io = now;
        if (!(payload[0] & 0x80u)) {
            if (payload[0] & 0x08u) {
                node->probing = false;
                s->phase = AHT_TRIGGER;
                s->phase_start = now - PERIOD_MS;
                return;
            }
            if (!s->init_sent) {
                if (!io->write(io->ctx, AHT_ADDRESS, init, sizeof(init))) break;
                s->init_sent = true;
            }
        }
        if (elapsed < POWER_ON_MS + TIMEOUT_MS) return;
        break;
    case AHT_TRIGGER:
        if (elapsed < PERIOD_MS) return;
        if (!io->write(io->ctx, AHT_ADDRESS, trigger, sizeof(trigger))) break;
        s->conversion_gen = node->generation;
        s->phase_start = s->last_io = now;
        s->phase = AHT_CONVERTING;
        return;
    case AHT_CONVERTING:
        if (elapsed < CONVERSION_MS || now - s->last_io < COMMAND_WAIT_MS) return;
        if (elapsed >= TIMEOUT_MS || !io->read(io->ctx, AHT_ADDRESS, payload, sizeof(payload))) break;
        s->last_io = now;
        if (payload[0] & 0x80u) return;
        if (!temperature_node_receive_aht20(node, s->conversion_gen, payload, sizeof(payload)) &&
            node->sample_flags != 2) break;
        /* Timestamp the trigger so a late read never makes old payload look fresh. */
        node->age_ms = (uint16_t)elapsed;
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
    }
    retry(s, node, now);
}
