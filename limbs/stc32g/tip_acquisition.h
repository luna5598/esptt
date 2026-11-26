#ifndef MOBAN_TIP_ACQUISITION_H
#define MOBAN_TIP_ACQUISITION_H

#include "temperature_node.h"

typedef struct {
    uint16_t low, high;       /* Inclusive raw ADC counts, in ascending band order. */
    uint8_t type;             /* 0 is empty, 255 is unknown. */
} tip_id_band;

typedef struct {
    const tip_id_band *bands;
    size_t count;
    uint16_t open_min, adc_max;
} tip_id_profile;

typedef struct {
    void *ctx;
    void (*power)(void *ctx, bool enable);
    bool (*read)(void *ctx, uint8_t address, uint8_t *data, size_t n);
    bool (*write)(void *ctx, uint8_t address, const uint8_t *data, size_t n);
} tip_io;

enum aht_phase { AHT_OFF, AHT_PROBE, AHT_TRIGGER, AHT_CONVERTING, AHT_RETRY };

typedef struct {
    temperature_node node;
    tip_id_profile profile;
    tip_io io;
    uint32_t last_tick, last_vote, phase_start, last_io;
    uint16_t conversion_gen;
    uint8_t candidate, votes;
    enum aht_phase phase;
    bool init_sent;
} tip_acquisition;

/* Cold start. The board owns the band table for the lifetime of the slot.
 * Power is an enable request; the board handles physical removal interlocking.
 * Local I2C callbacks perform one bounded transaction using a 7-bit address. */
bool tip_acquisition_init(tip_acquisition *slot, tip_id_profile profile, tip_io io);

/* Call from the node owner, serialized with BIND and register snapshots.
 * detached records a captured removal even if seated is already true again.
 * Read/clear that event atomically in the BSP; a pure ADC hookup can only
 * report a removal it actually sampled. now is a wrapping millisecond clock. */
void tip_acquisition_tick(tip_acquisition *slot, uint32_t now, bool seated,
                          bool detached, uint16_t adc);

#endif
