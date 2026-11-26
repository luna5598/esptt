#ifndef MOBAN_TEMPERATURE_NODE_H
#define MOBAN_TEMPERATURE_NODE_H

#include "limb_protocol.h"

typedef struct {
    uint32_t session;
    uint16_t generation, sequence, age_ms;
    int16_t temperature, humidity;
    uint8_t type, sample_flags;
    bool probing;
} temperature_node;

/* Zero-initialize at reset. Serialize these operations with register snapshots.
 * Report stable transitions from the accessory detector, including the empty
 * interval when swapping identical tips. Type 0 means empty, 255 unknown. */
void temperature_node_set_tip(temperature_node *node, uint8_t type);

/* Pass elapsed milliseconds from the node clock, including time between polls. */
void temperature_node_advance(temperature_node *node, uint32_t elapsed_ms);

/* Capture generation at measurement trigger. Pass the complete seven-byte
 * AHT20 response (status, humidity, temperature, CRC) at completion. */
bool temperature_node_receive_aht20(temperature_node *node, uint16_t generation,
                                   const uint8_t *frame, size_t length);

/* Read one register snapshot for a pointer write followed by repeated START.
 * Deliver a complete BIND or RELEASE transaction after STOP. */
bool temperature_node_read(const temperature_node *node, uint8_t reg,
                           uint8_t *data, size_t length);
bool temperature_node_write(temperature_node *node, const uint8_t *frame, size_t length);

#endif
