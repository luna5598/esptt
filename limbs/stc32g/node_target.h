#ifndef MOBAN_NODE_TARGET_H
#define MOBAN_NODE_TARGET_H

#include "temperature_node.h"

/* The I2C ISR owns the cursor, command buffer and session. The foreground
 * publishes with interrupts masked. The reply is latched at pointer write. */
typedef struct {
    temperature_node published;
    uint8_t rx[LIMB_BIND_FRAME_BYTES], reply[LIMB_SAMPLE_BYTES];
    uint8_t received, length, cursor;
} node_target;

void node_target_publish(node_target *target, const temperature_node *sample);
void node_target_start(node_target *target);
uint8_t node_target_receive(node_target *target, uint8_t octet);
uint8_t node_target_next(node_target *target);
void node_target_stop(node_target *target);
#endif
