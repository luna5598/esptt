#ifndef MOBAN_R01_PINS_H
#define MOBAN_R01_PINS_H
#include <STC32G.H>

#define TIP_SDA P16
#define TIP_SCL P17
#define TIP_SEATED P37
#define TIP_ARMED P36
#define TIP_PWR_REQ P35
#define TIP_ARM_CLK P34
#define TIP_IO_REQ P20
#define TIP_ADC_CHANNEL 0
/* P1.4/P1.5 upstream SDA/SCL, P1.6/P1.7 downstream SDA/SCL. */
#define P1_BUS_OD 0xf0u
#define P1_TIP_INPUTS 0x09u
#define P3_LATCH_INPUTS 0xc0u
#define P3_LATCH_OUTPUTS 0x30u
#define P2_IO_OUTPUT 0x01u
#endif
