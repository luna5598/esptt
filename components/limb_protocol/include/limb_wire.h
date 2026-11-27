#ifndef MOBAN_LIMB_WIRE_H
#define MOBAN_LIMB_WIRE_H

#include <stddef.h>
#if defined(__C251__) || defined(MOBAN_KEIL251)
#include <limits.h>
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned long uint32_t;
typedef signed long int32_t;
typedef unsigned char bool;
#define true 1
#define false 0
#define UINT16_MAX USHRT_MAX
#define INT16_MAX SHRT_MAX
#define LIMB_LOCAL static
typedef char limb_abi_check[(CHAR_BIT == 8 && sizeof(short) == 2 && sizeof(long) == 4) ? 1 : -1];
#else
#include <stdbool.h>
#include <stdint.h>
#if !defined(__STDC_VERSION__) && !defined(__cplusplus) && !defined(_MSC_VER)
#define LIMB_LOCAL static
#else
#define LIMB_LOCAL static inline
#endif
#endif

enum limb_register {
    LIMB_INFO = 0x00, LIMB_SAMPLE = 0x10, LIMB_BIND = 0x20,
    LIMB_RELEASE = 0x21, LIMB_CMD = 0x30, LIMB_ACK = 0x40
};
enum {
    LIMB_ADDRESS = 0x2a, LIMB_INFO_BYTES = 12, LIMB_SAMPLE_BYTES = 16,
    LIMB_ACK_BYTES = 11, LIMB_BIND_FRAME_BYTES = 8, LIMB_CMD_FRAME_BYTES = 14,
    LIMB_PRESENT = 1, LIMB_READY = 2, LIMB_BOUND = 4,
    LIMB_TEMP_HUM = 2, LIMB_DISTANCE = 3, LIMB_TEMP_HUM_CAP = 1 << 1, LIMB_DISTANCE_CAP = 1 << 2
};
LIMB_LOCAL uint16_t limb_sensor_cap(uint8_t type)
{
    if (type == LIMB_TEMP_HUM) return LIMB_TEMP_HUM_CAP;
    if (type == LIMB_DISTANCE) return LIMB_DISTANCE_CAP;
    return 0;
}

LIMB_LOCAL uint8_t limb_crc_byte(uint8_t crc, uint8_t byte)
{
    unsigned shift;
    crc ^= byte;
    for (shift = 0; shift < 8; shift++) {
        crc = (uint8_t)((crc & 0x80) ? ((unsigned)crc << 1) ^ 7u : (unsigned)crc << 1);
    }
    return crc;
}

/* The pointer register participates in CRC even on repeated-START reads. */
LIMB_LOCAL uint8_t limb_crc(uint8_t reg, const uint8_t *payload, size_t len)
{
    uint8_t crc = limb_crc_byte(0, reg);
    size_t i;
    for (i = 0; i < len; i++) crc = limb_crc_byte(crc, payload[i]);
    return crc;
}

LIMB_LOCAL uint16_t limb_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

LIMB_LOCAL void limb_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

LIMB_LOCAL void limb_put32(uint8_t *p, uint32_t value)
{
    limb_put16(p, (uint16_t)value);
    limb_put16(p + 2, (uint16_t)(value >> 16));
}

#undef LIMB_LOCAL
#endif
