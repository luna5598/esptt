#ifndef MOBAN_AHT20_H
#define MOBAN_AHT20_H
#include "temperature_node.h"
#include "tip_io.h"

enum aht_phase { AHT_PROBE, AHT_TRIGGER, AHT_CONVERTING, AHT_RETRY };
typedef struct {
    uint32_t phase_start, last_io;
    uint16_t conversion_gen;
    enum aht_phase phase;
    bool init_sent;
} aht20;

void aht20_start(aht20 *s, uint32_t now);
void aht20_tick(aht20 *s, const tip_io *io, temperature_node *node, uint32_t now);
#endif
