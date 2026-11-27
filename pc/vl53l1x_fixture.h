#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t registers[0x88];
    uint16_t model_id, pll;
    uint8_t stream, status;
    bool running, ready;
    uint32_t started, period;
} vl53_fixture;

void vl53_fixture_reset(vl53_fixture *f);
bool vl53_fixture_write(vl53_fixture *f, uint32_t now, const uint8_t *bytes, size_t n);
bool vl53_fixture_read(vl53_fixture *f, uint32_t now, uint16_t reg, uint8_t *out,
                       size_t n, uint16_t distance, bool stuck);
