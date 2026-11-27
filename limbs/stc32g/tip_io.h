#ifndef MOBAN_TIP_IO_H
#define MOBAN_TIP_IO_H
#include "limb_wire.h"

typedef struct {
    void *ctx;
    void (*power)(void *ctx, bool enable);
    bool (*read)(void *ctx, uint8_t address, uint8_t *payload, size_t n);
    bool (*write)(void *ctx, uint8_t address, const uint8_t *payload, size_t n);
    /* Register pointer followed by repeated START and read, all in one transfer. */
    bool (*read_reg16)(void *ctx, uint8_t address, uint16_t reg, uint8_t *payload, size_t n);
} tip_io;

#endif
