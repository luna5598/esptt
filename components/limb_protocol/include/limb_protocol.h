#ifndef MOBAN_LIMB_PROTOCOL_H
#define MOBAN_LIMB_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum limb_register {
    LIMB_INFO = 0x00, LIMB_SAMPLE = 0x10, LIMB_BIND = 0x20,
    LIMB_RELEASE = 0x21, LIMB_CMD = 0x30, LIMB_ACK = 0x40,
};
enum {
    LIMB_ADDRESS = 0x2a, LIMB_INFO_BYTES = 12, LIMB_SAMPLE_BYTES = 16,
    LIMB_ACK_BYTES = 11, LIMB_BIND_FRAME_BYTES = 8, LIMB_CMD_FRAME_BYTES = 14,
    LIMB_PRESENT = 1, LIMB_READY = 2, LIMB_BOUND = 4,
    LIMB_TEMP_HUM = 2, LIMB_TEMP_HUM_CAP = 1 << 1,
};
typedef struct {
    uint8_t type, flags, fault, firmware;
    uint16_t capabilities, tip_gen;
} limb_info;
typedef struct {
    uint16_t tip_gen, sequence, age_ms, edge_count;
    uint8_t flags, type, switch_state;
    int16_t a, b;
} limb_sample;
typedef struct {
    uint32_t session;
    uint16_t sequence, tip_gen;
    uint8_t status, reason;
} limb_ack;

static inline uint8_t limb_crc_byte(uint8_t crc, uint8_t byte)
{
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; bit++) {
        crc = (uint8_t)((crc & 0x80) ? ((unsigned)crc << 1) ^ 7u : (unsigned)crc << 1);
    }
    return crc;
}

/* The pointer register participates in CRC even on repeated-START reads. */
static inline uint8_t limb_crc(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t crc = limb_crc_byte(0, reg);
    for (size_t i = 0; i < len; i++) crc = limb_crc_byte(crc, data[i]);
    return crc;
}

static inline uint16_t limb_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline void limb_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static inline void limb_put32(uint8_t *p, uint32_t value)
{
    limb_put16(p, (uint16_t)value);
    limb_put16(p + 2, (uint16_t)(value >> 16));
}

static inline bool limb_decode_info(const uint8_t *p, size_t n, limb_info *out)
{
    if (n != LIMB_INFO_BYTES || limb_crc(LIMB_INFO, p, n - 1) != p[n - 1] ||
        p[0] != 0x4d || p[1] != 1 || p[2] != 1) return false;
    *out = (limb_info){.type=p[3], .flags=p[4], .capabilities=limb_u16(p+5),
                       .tip_gen=limb_u16(p+7), .fault=p[9], .firmware=p[10]};
    return true;
}

static inline bool limb_decode_sample(const uint8_t *p, size_t n, limb_sample *out)
{
    if (n != LIMB_SAMPLE_BYTES || limb_crc(LIMB_SAMPLE, p, n - 1) != p[n - 1]) return false;
    *out = (limb_sample){.tip_gen=limb_u16(p), .sequence=limb_u16(p+2), .flags=p[4],
                         .type=p[5], .a=(int16_t)limb_u16(p+6), .b=(int16_t)limb_u16(p+8),
                         .age_ms=limb_u16(p+10), .edge_count=limb_u16(p+12), .switch_state=p[14]};
    return true;
}

static inline bool limb_decode_ack(const uint8_t *p, size_t n, limb_ack *out)
{
    if (n != LIMB_ACK_BYTES || limb_crc(LIMB_ACK, p, n - 1) != p[n - 1] || p[8]>4) return false;
    *out = (limb_ack){.session=limb_u16(p) | ((uint32_t)limb_u16(p+2) << 16),
                      .sequence=limb_u16(p+4), .tip_gen=limb_u16(p+6), .status=p[8], .reason=p[9]};
    return true;
}

static inline bool limb_bind_frame(uint8_t reg, uint16_t gen, uint32_t session,
                                   uint8_t out[LIMB_BIND_FRAME_BYTES])
{
    if ((reg != LIMB_BIND && reg != LIMB_RELEASE) || !session) return false;
    out[0] = reg;
    limb_put16(out + 1, gen);
    limb_put32(out + 3, session);
    out[7] = limb_crc(reg, out + 1, 6);
    return true;
}

static inline bool limb_cmd_frame(uint32_t session, uint16_t seq, uint16_t gen,
                                  uint8_t opcode, uint16_t arg, uint8_t out[LIMB_CMD_FRAME_BYTES])
{
    if (!session || !seq) return false;
    if (!((opcode == 1 && arg >= 1 && arg <= 500) || (opcode == 2 && arg <= 1))) return false;
    out[0] = LIMB_CMD;
    limb_put32(out + 1, session);
    limb_put16(out + 5, seq);
    limb_put16(out + 7, gen);
    out[9] = opcode;
    limb_put16(out + 10, arg);
    out[12] = 0;
    out[13] = limb_crc(LIMB_CMD, out + 1, 12);
    return true;
}
#endif
