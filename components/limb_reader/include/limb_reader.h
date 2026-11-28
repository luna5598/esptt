#ifndef MOBAN_LIMB_READER_H
#define MOBAN_LIMB_READER_H

#include "limb_protocol.h"

typedef struct {
    void *ctx;
    bool (*read)(void *ctx, uint8_t reg, uint8_t *payload, size_t len);
    bool (*write)(void *ctx, const uint8_t *frame, size_t len);
} limb_transport;

typedef struct {
    uint32_t session;
    uint16_t tip_gen, last_sample;
    bool identified, has_sequence;
} limb_reader;

enum limb_result {
    LIMB_OFFLINE, LIMB_EMPTY_SLOT, LIMB_PENDING, LIMB_UNSUPPORTED, LIMB_FOREIGN_BINDING,
    LIMB_BAD_FRAME, LIMB_LINK_FAULT, LIMB_NO_SAMPLE, LIMB_UNCHANGED, LIMB_NEW_SAMPLE,
    LIMB_CHANNEL_FAULT, LIMB_BUS_UNAVAILABLE,
};

typedef struct {
    enum limb_result result;
    bool replaced;
    limb_sample sample;
} limb_poll_result;

/* Call from one bus owner after selecting exactly one physical channel.
 * Keep the pending session across failed reads so a lost BIND ACK can be retried. */
limb_poll_result limb_poll(limb_reader *reader, const limb_transport *io, uint32_t next_session);

#endif
