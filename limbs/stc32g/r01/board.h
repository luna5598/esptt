#ifndef MOBAN_R01_BOARD_H
#define MOBAN_R01_BOARD_H
#include "tip_acquisition.h"

/* R01 QFN32 netlist. Set the ISP oscillator to the matching system clock. */
#define R01_CLOCK_HZ 24000000UL
void board_init(void);
uint32_t board_millis(void);
uint16_t board_tip_adc(void);
bool board_seated(void);
bool board_detached(void);
void board_tip_power(void *ctx, bool on);
bool board_sensor_read(void *ctx, uint8_t address, uint8_t *bytes, size_t length);
bool board_sensor_write(void *ctx, uint8_t address, const uint8_t *bytes, size_t length);
bool board_sensor_read_reg16(void *ctx, uint8_t address, uint16_t reg, uint8_t *bytes, size_t length);
#endif
