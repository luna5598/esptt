#ifndef MOBAN_LIMB_PROTOCOL_H
#define MOBAN_LIMB_PROTOCOL_H

#include "limb_wire.h"

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
