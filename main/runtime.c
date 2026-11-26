#include <stdlib.h>
#include "runtime.h"
#include "slot_observation.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t lock;
static uint64_t boot;
static bool bus_ready;
static enum limb_result status[MO_SLOT_COUNT];

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }

void device_init(void)
{
    lock = xSemaphoreCreateMutex();
    if (!lock) abort();
    do { esp_fill_random(&boot, sizeof(boot)); } while (!boot);
    mo_init(boot);
}

void device_bus_ready(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bus_ready = true;
    xSemaphoreGive(lock);
}

void device_observe(unsigned slot, const limb_poll_result *poll)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    mo_observe_slot(boot, slot, now_ms(), poll);
    status[slot] = poll->result;
    xSemaphoreGive(lock);
}

void device_snapshot_read(device_snapshot *out)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    out->boot = boot;
    out->now = now_ms();
    out->bus_ready = bus_ready;
    out->course = *mo_course();
    for (unsigned i = 0; i < MO_SLOT_COUNT; i++) {
        out->slots[i] = *mo_slot(i);
        out->status[i] = status[i];
    }
    xSemaphoreGive(lock);
}

int device_course_action(bool resume, uint64_t requested_boot, unsigned slot, uint32_t gen, uint32_t run)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    int rc = (resume ? mo_resume : mo_start)(requested_boot, slot, gen, now_ms(), run);
    xSemaphoreGive(lock);
    return rc;
}
