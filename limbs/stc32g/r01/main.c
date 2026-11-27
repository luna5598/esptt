#include "board.h"
#include "pins.h"
#include "tip_profile.h"
#include "node_target.h"

static tip_acquisition acquisition;
static node_target upstream;
static bool address_phase;
static const tip_io sensor_bus = {0, board_tip_power, board_sensor_read, board_sensor_write,
                                  board_sensor_read_reg16};

void limb_i2c_irq(void) interrupt I2C_VECTOR
{
    uint8_t flags = I2CSLST;
    if (flags & STAIF) {
        I2CSLST &= ~STAIF;
        node_target_start(&upstream);
        address_phase = true;
    } else if (flags & RXIF) {
        I2CSLST &= ~RXIF;
        if (address_phase) address_phase = false;
        else I2CTXD = node_target_receive(&upstream, I2CRXD);
    } else if (flags & TXIF) {
        I2CSLST &= ~TXIF;
        I2CTXD = (flags & SLACKI) ? 0xff : node_target_next(&upstream);
    } else if (flags & STOIF) {
        I2CSLST &= ~STOIF;
        node_target_stop(&upstream);
        I2CTXD = 0xff;
    }
}

void main(void)
{
    uint32_t now, previous;
    uint16_t adc;
    bool seated, detached;
    board_init();
    if (!tip_acquisition_init(&acquisition, r01_tip_profile, sensor_bus)) return;
    I2CCFG = 0x80;
    I2CSLADR = LIMB_ADDRESS << 1;
    I2CSLST = 0;
    I2CTXD = 0xff;
    I2CSLCR = ESTAI | ERXI | ETXI | ESTOI;
    EA = 1;
    previous = board_millis();
    for (;;) {
        now = board_millis();
        if (now == previous) continue;
        previous = now;
        seated = board_seated();
        adc = seated ? board_tip_adc() : UINT16_MAX;
        detached = board_detached();
        tip_acquisition_tick(&acquisition, now, seated, detached, adc);
        /* Keep software I2C and the ADC outside the interrupt exclusion window.
         * An ISR-accepted BIND survives the next publication of this generation. */
        EA = 0;
        node_target_publish(&upstream, &acquisition.node);
        EA = 1;
    }
}
