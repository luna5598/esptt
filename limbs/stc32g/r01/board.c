#include "board.h"
#include "pins.h"
#include <intrins.h>

static volatile uint32_t ticks;
static bool arm_requested;
static uint32_t enabled_at;
#define T0_RELOAD (65536UL - R01_CLOCK_HZ / 1000UL)

void timer0_irq(void) interrupt 1
{
    ticks++;
}

uint32_t board_millis(void)
{
    uint8_t saved = EA;
    uint32_t value;
    EA = 0;
    value = ticks;
    EA = saved;
    return value;
}

void board_init(void)
{
    EA = 0;
    EAXFR = 1;
    TIP_IO_REQ = 0;
    TIP_PWR_REQ = 0;
    TIP_ARM_CLK = 0;
    TIP_SDA = TIP_SCL = 1;
    /* High impedance inputs, open-drain buses and push-pull requests. */
    P1M1 |= P1_BUS_OD | P1_TIP_INPUTS;
    P1M0 = (P1M0 & ~P1_TIP_INPUTS) | P1_BUS_OD;
    P3M1 = (P3M1 & ~P3_LATCH_OUTPUTS) | P3_LATCH_INPUTS;
    P3M0 = (P3M0 & ~P3_LATCH_INPUTS) | P3_LATCH_OUTPUTS;
    P2M1 &= ~P2_IO_OUTPUT;
    P2M0 |= P2_IO_OUTPUT;
    P1PU &= ~(P1_BUS_OD | P1_TIP_INPUTS);
    I2C_S0 = I2C_S1 = 0;
    ADCTIM = 0x3f;
    ADCCFG = 0x2f;
    ADC_CONTR = 0x80;
    TR0 = 0;
    AUXR |= 0x80;
    TMOD &= 0xf0;
    TL0 = (uint8_t)T0_RELOAD;
    TH0 = (uint8_t)(T0_RELOAD >> 8);
    TF0 = 0;
    ET0 = 1;
    TR0 = 1;
}

bool board_seated(void) { return TIP_SEATED != 0; }
bool board_detached(void) { return arm_requested && !TIP_ARMED; }

uint16_t board_tip_adc(void)
{
    uint32_t start = board_millis();
    ADC_CONTR = 0xc0u | TIP_ADC_CHANNEL;
    _nop_(); _nop_(); _nop_(); _nop_();
    while (!ADC_FLAG) {
        if (board_millis() - start >= 2) return UINT16_MAX;
    }
    ADC_FLAG = 0;
    return ((uint16_t)ADC_RES << 8) | ADC_RESL;
}

void board_tip_power(void *ctx, bool on)
{
    (void)ctx;
    TIP_IO_REQ = 0;
    TIP_PWR_REQ = 0;
    TIP_ARM_CLK = 0;
    TIP_SDA = TIP_SCL = 1;
    arm_requested = on;
    if (!on || !TIP_SEATED) return;
    TIP_ARM_CLK = 1;
    _nop_();
    TIP_ARM_CLK = 0;
    if (!TIP_ARMED) return;
    TIP_PWR_REQ = 1;
    enabled_at = board_millis();
}

static bool live(void)
{
    return arm_requested && TIP_SEATED && TIP_ARMED && TIP_PWR_REQ;
}

/* Bounded bit-banged I2C on JT2. The upstream slave and timer may interrupt
 * this foreground transfer. The hardware latch removes power/IO on detach. */
static void half_cycle(void)
{
    unsigned char volatile loops = 24;
    while (--loops) _nop_();
}

static bool raise_clock(void)
{
    uint32_t start = board_millis();
    TIP_SCL = 1;
    while (!TIP_SCL) {
        if (!live() || board_millis() - start >= 2) return false;
    }
    half_cycle();
    return live();
}

static bool write_octet(uint8_t octet)
{
    uint8_t mask;
    bool ack;
    for (mask = 0x80; mask; mask >>= 1) {
        TIP_SDA = (octet & mask) != 0;
        half_cycle();
        if (!raise_clock()) return false;
        TIP_SCL = 0;
    }
    TIP_SDA = 1;
    half_cycle();
    if (!raise_clock()) return false;
    ack = !TIP_SDA;
    TIP_SCL = 0;
    return ack;
}

static bool begin_transfer(uint8_t address_byte)
{
    if (!live() || board_millis() - enabled_at < 5) return false;
    TIP_IO_REQ = 1;
    TIP_SDA = 1;
    if (!raise_clock() || !TIP_SDA) return false;
    TIP_SDA = 0;
    half_cycle();
    TIP_SCL = 0;
    return write_octet(address_byte);
}

static bool end_transfer(bool ok)
{
    bool clock_high;
    TIP_SCL = 0;
    TIP_SDA = 0;
    half_cycle();
    clock_high = raise_clock();
    TIP_SDA = 1;
    half_cycle();
    return ok && clock_high && TIP_SDA;
}

bool board_sensor_write(void *ctx, uint8_t address, const uint8_t *bytes, size_t length)
{
    bool ok = begin_transfer((uint8_t)(address << 1));
    (void)ctx;
    while (ok && length--) ok = write_octet(*bytes++);
    return end_transfer(ok);
}

bool board_sensor_read(void *ctx, uint8_t address, uint8_t *bytes, size_t length)
{
    uint8_t n, octet;
    bool ok = begin_transfer((uint8_t)((address << 1) | 1));
    (void)ctx;
    while (ok && length) {
        octet = 0;
        TIP_SDA = 1;
        for (n = 0; n < 8; n++) {
            half_cycle();
            if (!raise_clock()) return end_transfer(false);
            octet = (uint8_t)((octet << 1) | (TIP_SDA != 0));
            TIP_SCL = 0;
        }
        *bytes++ = octet;
        --length;
        TIP_SDA = length == 0;
        half_cycle();
        ok = raise_clock();
        TIP_SCL = 0;
    }
    return end_transfer(ok);
}
