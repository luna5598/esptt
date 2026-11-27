#include "vl53l1x.h"
#include <string.h>

enum {
    ADDRESS = 0x29, BOOT = 0x00e5, MODEL_ID = 0x010f,
    CONFIG_START = 0x002d, READY = 0x0031, START = 0x0087, CLEAR = 0x0086,
    RESULT = 0x0089, OSC = 0x00de, PERIOD_REG = 0x006c,
    CONFIG_CHUNK = 8, POLL_MS = 10, BOOT_WAIT_MS = 10, BOOT_TIMEOUT_MS = 300,
    CAL_TIMEOUT_MS = 350, BUDGET_MS = 50, PERIOD_MS = 80, FRAME_TIMEOUT_MS = 200,
    RETRY_MS = 500
};

/* ULD default register values from stm32duino/VL53L1X, revision 5aa46e74.
 * COPYRIGHT(c) 2018 STMicroelectronics
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright notice,
 *      this list of conditions and the following disclaimer in the documentation
 *      and/or other materials provided with the distribution.
 *   3. Neither the name of STMicroelectronics nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* Pololu 3415 uses a 2.8 V sensor-side I2C pull-up: set EXTSUP_CONFIG (0x2e). */
static const uint8_t defaults[] = {
    0x00, 0x01, 0x00, 0x01, 0x02, 0x00, 0x02, 0x08, 0x00, 0x08, 0x10, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x20, 0x0b, 0x00, 0x00, 0x02, 0x0a, 0x21, 0x00, 0x00, 0x05, 0x00,
    0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x38, 0xff, 0x01, 0x00, 0x08, 0x00,
    0x00, 0x01, 0xcc, 0x0f, 0x01, 0xf1, 0x0d, 0x01, 0x68, 0x00, 0x80, 0x08,
    0xb8, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x0f, 0x0d, 0x0e, 0x0e, 0x00, 0x00, 0x02, 0xc7, 0xff,
    0x9b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00
};

static bool write_reg(const tip_io *io, uint16_t reg, const uint8_t *src, size_t n)
{
    uint8_t frame[2 + CONFIG_CHUNK];
    if (n > CONFIG_CHUNK) return false;
    frame[0] = (uint8_t)(reg >> 8);
    frame[1] = (uint8_t)reg;
    memcpy(frame + 2, src, n);
    return io->write(io->ctx, ADDRESS, frame, n + 2);
}

static bool write_byte(const tip_io *io, uint16_t reg, uint8_t value)
{
    return write_reg(io, reg, &value, 1);
}

static uint16_t big_endian(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static void phase(vl53l1x *s, enum vl53_phase next, uint32_t now)
{
    s->phase = next;
    s->since = s->last_poll = now;
}

void vl53l1x_start(vl53l1x *s, uint32_t now, uint16_t generation)
{
    s->generation = generation;
    s->has_stream = false;
    s->config_offset = 0;
    phase(s, VL_BOOT, now);
}

static bool begin_ranging(const tip_io *io)
{
    uint8_t raw[4];
    uint16_t pll;
    uint32_t period;
    /* ULD long-mode 50 ms A/B timeouts; the intermeasurement clock is per chip. */
    static const uint8_t timeout_a[] = {0x00, 0xad}, timeout_b[] = {0x00, 0xc6};
    if (!write_byte(io, CLEAR, 1) || !write_byte(io, START, 0) ||
        !write_byte(io, 0x0008, 9) || !write_byte(io, 0x000b, 0) ||
        !write_reg(io, 0x005e, timeout_a, 2) || !write_reg(io, 0x0061, timeout_b, 2) ||
        !io->read_reg16(io->ctx, ADDRESS, OSC, raw, 2)) return false;
    pll = big_endian(raw) & 0x03ffu;
    if (!pll) return false;
    period = (uint32_t)pll * PERIOD_MS * 1075UL / 1000UL;
    raw[0] = (uint8_t)(period >> 24);
    raw[1] = (uint8_t)(period >> 16);
    raw[2] = (uint8_t)(period >> 8);
    raw[3] = (uint8_t)period;
    return write_reg(io, PERIOD_REG, raw, 4) && write_byte(io, START, 0x40);
}

void vl53l1x_tick(vl53l1x *s, const tip_io *io, temperature_node *node, uint32_t now)
{
    uint32_t elapsed = now - s->since;
    uint8_t raw[15], stream;
    size_t n;
    if (node->generation != s->generation || node->type != LIMB_DISTANCE) return;
    switch (s->phase) {
    case VL_RETRY:
        if (elapsed >= RETRY_MS) vl53l1x_start(s, now, node->generation);
        return;
    case VL_BOOT:
        if (elapsed < BOOT_WAIT_MS || now - s->last_poll < POLL_MS) return;
        s->last_poll = now;
        if (elapsed >= BOOT_TIMEOUT_MS || !io->read_reg16(io->ctx, ADDRESS, BOOT, raw, 1)) break;
        if (!(raw[0] & 1)) return;
        if (!io->read_reg16(io->ctx, ADDRESS, MODEL_ID, raw, 2) || big_endian(raw) != 0xeacc)
            break;
        /* A retried sensor can already be ranging. Stop before reconfiguration. */
        if (!write_byte(io, START, 0)) break;
        phase(s, VL_CONFIGURE, now);
        return;
    case VL_CONFIGURE:
        n = sizeof(defaults) - s->config_offset;
        if (n > CONFIG_CHUNK) n = CONFIG_CHUNK;
        if (!write_reg(io, CONFIG_START + s->config_offset, defaults + s->config_offset, n)) break;
        s->config_offset += (uint8_t)n;
        if (s->config_offset == sizeof(defaults)) {
            if (!write_byte(io, START, 0x40)) break;
            phase(s, VL_CALIBRATE, now);
        }
        return;
    case VL_CALIBRATE:
    case VL_RANGING:
        if (now - s->last_poll < POLL_MS) return;
        s->last_poll = now;
        if (elapsed >= (uint32_t)(s->phase == VL_CALIBRATE ? CAL_TIMEOUT_MS : FRAME_TIMEOUT_MS) ||
            !io->read_reg16(io->ctx, ADDRESS, READY, raw, 1)) break;
        /* The ULD default config programs active-high new-sample ready. */
        if (!(raw[0] & 1)) return;
        if (s->phase == VL_CALIBRATE) {
            if (!begin_ranging(io)) break;
            phase(s, VL_RANGING, now);
            return;
        }
        /* One coherent burst: status 0x89, stream count 0x8b, distance 0x96. */
        if (!io->read_reg16(io->ctx, ADDRESS, RESULT, raw, sizeof(raw)) ||
            !write_byte(io, CLEAR, 1)) break;
        stream = raw[2];
        if (s->has_stream && s->stream == stream) return;
        s->has_stream = true;
        s->stream = stream;
        limb_node_receive_range(node, s->generation, big_endian(raw + 13), (raw[0] & 31u) == 9,
                                (uint16_t)(elapsed + BUDGET_MS));
        s->since = now;
        return;
    }
    /* Suppress cached measurements immediately; preserve the upstream binding
     * during a retry on the same physical tip. A detach retires the generation. */
    node->sample_flags = 0;
    phase(s, VL_RETRY, now);
}
