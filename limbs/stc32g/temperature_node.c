#include "temperature_node.h"
#include <string.h>

void temperature_node_set_tip(temperature_node *node, uint8_t type)
{
    uint16_t next;
    if (node->type == type) return;
    next = (uint16_t)(node->generation + 1u);
    memset(node, 0, sizeof(*node));
    node->type = type;
    node->generation = next;
}

void temperature_node_advance(temperature_node *node, uint32_t elapsed_ms)
{
    uint32_t remaining = (uint32_t)UINT16_MAX - node->age_ms;
    node->age_ms = elapsed_ms >= remaining ? UINT16_MAX :
                   (uint16_t)(node->age_ms + elapsed_ms);
}

static uint8_t aht20_crc(const uint8_t *frame)
{
    uint8_t crc = 0xff;
    unsigned i, bit;
    for (i = 0; i < 6; i++) {
        crc ^= frame[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (uint8_t)((crc & 0x80u) ? (crc << 1) ^ 0x31u : (unsigned)crc << 1);
        }
    }
    return crc;
}

bool temperature_node_receive_aht20(temperature_node *node, uint16_t generation,
                                   const uint8_t *frame, size_t length)
{
    uint32_t raw_humidity, raw_temperature;
    int32_t temperature;
    if (generation != node->generation || node->type != LIMB_TEMP_HUM) return false;
    if (length != 7 || aht20_crc(frame) != frame[6] || !(frame[0] & 0x08u)) {
        node->sample_flags = 0;
        return false;
    }
    if (frame[0] & 0x80u) return false; /* Conversion still in progress. */
    raw_humidity = ((uint32_t)frame[1] << 12) | ((uint32_t)frame[2] << 4) | (frame[3] >> 4);
    raw_temperature = ((uint32_t)(frame[3] & 0x0fu) << 16) | ((uint32_t)frame[4] << 8) | frame[5];

    /* 20000 / 2^20 = 625 / 32768. The largest product fits in uint32_t. */
    temperature = (int32_t)((raw_temperature * 625u + 16384u) >> 15) - 5000;
    node->temperature = (int16_t)temperature;
    node->humidity = (int16_t)((raw_humidity * 625u + 32768u) >> 16);
    node->sequence++;
    node->age_ms = 0;
    node->sample_flags = (temperature < -4000 || temperature > 8500) ? 2u : 1u;
    return node->sample_flags == 1u;
}

bool temperature_node_read(const temperature_node *node, uint8_t reg, uint8_t *data, size_t length)
{
    size_t expected;
    switch (reg) {
    case LIMB_INFO: expected = LIMB_INFO_BYTES; break;
    case LIMB_ACK: expected = LIMB_ACK_BYTES; break;
    case LIMB_SAMPLE: expected = LIMB_SAMPLE_BYTES; break;
    default: return false;
    }
    if (length != expected) return false;
    memset(data, 0, length);
    switch (reg) {
    case LIMB_INFO:
        data[0] = 0x4d;
        data[1] = 1;
        data[2] = 1;
        data[3] = node->type;
        if (node->type) data[4] |= LIMB_PRESENT;
        if (node->type == LIMB_TEMP_HUM) {
            if (!node->probing) data[4] |= LIMB_READY;
            limb_put16(data + 5, LIMB_TEMP_HUM_CAP);
        }
        if (node->session) data[4] |= LIMB_BOUND;
        limb_put16(data + 7, node->generation);
        data[10] = 1;
        break;
    case LIMB_ACK:
        limb_put32(data, node->session);
        limb_put16(data + 6, node->generation);
        break;
    case LIMB_SAMPLE:
        limb_put16(data, node->generation);
        limb_put16(data + 2, node->sequence);
        data[4] = node->sample_flags;
        data[5] = node->type;
        limb_put16(data + 6, (uint16_t)node->temperature);
        limb_put16(data + 8, (uint16_t)node->humidity);
        limb_put16(data + 10, node->age_ms);
        break;
    }
    data[length - 1] = limb_crc(reg, data, length - 1);
    return true;
}

bool temperature_node_write(temperature_node *node, const uint8_t *frame, size_t length)
{
    uint32_t session;
    if (length != LIMB_BIND_FRAME_BYTES ||
        (frame[0] != LIMB_BIND && frame[0] != LIMB_RELEASE) ||
        limb_crc(frame[0], frame + 1, length - 2) != frame[length - 1] ||
        limb_u16(frame + 1) != node->generation) return false;
    session = (uint32_t)limb_u16(frame + 3) | ((uint32_t)limb_u16(frame + 5) << 16);
    if (!session || (node->session && session != node->session)) return false;
    if (frame[0] == LIMB_RELEASE) {
        node->session = 0;
        return true;
    }
    if (node->type != LIMB_TEMP_HUM || node->probing) return false;
    node->session = session;
    return true;
}
