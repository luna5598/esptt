#ifndef MOBAN_R01_TIP_PROFILE_H
#define MOBAN_R01_TIP_PROFILE_H
#include "tip_acquisition.h"

/* 12-bit ADC, 10k pull-up, accessory IDs 2.7k and 4.3k to ground.
 * Nominal windows; confirm tolerances on the assembled board. */
static const tip_id_band r01_bands[] = {{791, 951, LIMB_TEMP_HUM}, {1150, 1310, LIMB_DISTANCE}};
static const tip_id_profile r01_tip_profile = {r01_bands, sizeof(r01_bands)/sizeof(r01_bands[0]), 3950, 4095};
#endif
