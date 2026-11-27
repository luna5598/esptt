#include "vl53l1x_fixture.h"
#include <string.h>

void vl53_fixture_reset(vl53_fixture *f)
{
    *f = (vl53_fixture){.model_id = 0xeacc, .pll = 512, .status = 9, .period = 100};
}

bool vl53_fixture_write(vl53_fixture *f, uint32_t now, const uint8_t *p, size_t n)
{
    if (n < 3) return false;
    uint16_t reg = (uint16_t)((unsigned)p[0] << 8 | p[1]);
    if ((size_t)reg + n - 2 > sizeof(f->registers)) return false;
    memcpy(f->registers + reg, p + 2, n - 2);
    if (reg == 0x0087 && n == 3) {
        f->running = p[2] == 0x40;
        f->started = now;
        f->ready = false;
    } else if (reg == 0x0086 && n == 3 && p[2] == 1) {
        f->ready = false;
        f->started = now;
    } else if (reg == 0x006c && n == 6 && f->pll) {
        uint32_t ticks = (uint32_t)p[2] << 24 | (uint32_t)p[3] << 16 |
                         (uint32_t)p[4] << 8 | p[5];
        f->period = ticks * 1000 / ((uint32_t)f->pll * 1075);
    }
    return true;
}

bool vl53_fixture_read(vl53_fixture *f, uint32_t now, uint16_t reg, uint8_t *out,
                       size_t n, uint16_t mm, bool stuck)
{
    if (reg == 0x00e5 && n == 1) out[0] = 1;
    else if ((reg == 0x010f || reg == 0x00de) && n == 2) {
        uint16_t value = reg == 0x010f ? f->model_id : f->pll;
        out[0] = (uint8_t)(value >> 8);
        out[1] = (uint8_t)value;
    } else if (reg == 0x0031 && n == 1) {
        if (!f->ready && !stuck && f->running && now - f->started >= f->period) {
            f->ready = true;
            f->stream++;
        }
        out[0] = f->ready && !stuck;
    } else if (reg == 0x0089 && n == 15 && f->ready) {
        memset(out, 0, n);
        out[0] = mm ? f->status : 4;
        out[2] = f->stream;
        out[13] = (uint8_t)(mm >> 8);
        out[14] = (uint8_t)mm;
    } else return false;
    return true;
}
