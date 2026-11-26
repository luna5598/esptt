#ifndef MOBAN_SLOT_OBSERVATION_H
#define MOBAN_SLOT_OBSERVATION_H

#include "limb_reader.h"
#include "moban_core.h"

void mo_observe_slot(uint64_t boot, unsigned slot, uint64_t now, const limb_poll_result *poll);
const char *mo_link_name(enum limb_result result);

#endif
