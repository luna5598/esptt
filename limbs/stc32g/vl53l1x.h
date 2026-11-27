#ifndef MOBAN_VL53L1X_H
#define MOBAN_VL53L1X_H
#include "temperature_node.h"
#include "tip_io.h"

enum vl53_phase { VL_BOOT, VL_CONFIGURE, VL_CALIBRATE, VL_RANGING, VL_RETRY };
typedef struct {
    uint32_t since, last_poll;
    uint16_t generation;
    uint8_t config_offset, stream;
    bool has_stream;
    enum vl53_phase phase;
} vl53l1x;

void vl53l1x_start(vl53l1x *s, uint32_t now, uint16_t generation);
void vl53l1x_tick(vl53l1x *s, const tip_io *io, temperature_node *node, uint32_t now);
#endif
