#include "sdkconfig.h"
#include "runtime.h"
#include "esp_err.h"
#include "esp_log.h"

#if CONFIG_MOBAN_LIMB_BUS
#include "limb_scan.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_master_dev_handle_t mux, node;
static i2c_master_bus_handle_t bus;
static limb_reader readers[MO_SLOT_COUNT];
static limb_scan scan;
static uint64_t power_wait_ms[MO_SLOT_COUNT];
static const int enable_pin[MO_SLOT_COUNT] = {
    CONFIG_MOBAN_SLOT_0_EN, CONFIG_MOBAN_SLOT_1_EN, CONFIG_MOBAN_SLOT_2_EN, CONFIG_MOBAN_SLOT_3_EN
};
static bool powered_off[MO_SLOT_COUNT];
#ifdef CONFIG_MOBAN_SLOT_EN_LOW
#define SLOT_POWER_ON 0
#else
#define SLOT_POWER_ON 1
#endif
static uint32_t session;
static const int reset_pin = CONFIG_MOBAN_MUX_RESET;

static bool isolate(void)
{
    if (gpio_set_level(reset_pin, 0) != ESP_OK) return false;
    esp_rom_delay_us(10);
    if (gpio_set_level(reset_pin, 1) != ESP_OK) return false;
    esp_rom_delay_us(10);
    return true;
}

static bool read_node(void *ctx, uint8_t reg, uint8_t *data, size_t n)
{
    bool *ok = ctx;
    *ok = i2c_master_transmit_receive(node, &reg, 1, data, n, 12) == ESP_OK;
    return *ok;
}

static bool write_node(void *ctx, const uint8_t *data, size_t n)
{
    bool *ok = ctx;
    *ok = i2c_master_transmit(node, data, n, 12) == ESP_OK;
    return *ok;
}

static bool select_slot(void *ctx, unsigned slot)
{
    (void)ctx;
    uint8_t channel = (uint8_t)(1u << slot);
    return i2c_master_transmit(mux, &channel, 1, 12) == ESP_OK;
}

static bool recover_mux(void *ctx)
{
    (void)ctx;
    uint8_t mask;
    if (!isolate() || i2c_master_bus_reset(bus) != ESP_OK) return false;
    if (i2c_master_receive(mux, &mask, 1, 12) != ESP_OK || (mask & 0x0f)) return false;
    /* Verify the upstream write path too, without connecting a branch. */
    uint8_t off = 0;
    return i2c_master_transmit(mux, &off, 1, 12) == ESP_OK;
}

static bool poll_node(void *ctx, unsigned slot, limb_poll_result *out)
{
    (void)ctx;
    bool ok = true;
    const limb_transport transport = {.ctx=&ok, .read=read_node, .write=write_node};
    if (++session == 0) session++;
    *out = limb_poll(&readers[slot], &transport, session);
    return ok;
}

static void poll_task(void *arg)
{
    (void)arg;
    const limb_scan_io io = {.select=select_slot, .recover=recover_mux, .poll=poll_node};
    for (;;) {
        for (unsigned i=0; i<MO_SLOT_COUNT; ++i) {
            uint64_t now = (uint64_t)esp_timer_get_time()/1000;
            if (now < power_wait_ms[i]) continue;
            if (powered_off[i]) {
                if (gpio_set_level(enable_pin[i], SLOT_POWER_ON) == ESP_OK) powered_off[i] = false;
                power_wait_ms[i] = now + 150;
                continue;
            }
            limb_poll_result result;
            enum scan_event event = limb_scan_slot(&scan, &io, i, now, &result);
            if (event == SCAN_BUS_DOWN) device_bus_fault();
            if (event != SCAN_OBSERVED) continue;
            if (result.result == LIMB_FOREIGN_BINDING && enable_pin[i] >= 0 &&
                gpio_set_level(enable_pin[i], !SLOT_POWER_ON) == ESP_OK) {
                powered_off[i] = true;
                power_wait_ms[i] = scan.slot_retry_ms[i];
                readers[i] = (limb_reader){0};
            }
            device_observe(i, &result);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static bool valid_output(int pin)
{
    return pin >= 0 && pin < GPIO_NUM_MAX && GPIO_IS_VALID_OUTPUT_GPIO(pin);
}

esp_err_t limb_bus_start(void)
{
    int sda = CONFIG_MOBAN_SDA, scl = CONFIG_MOBAN_SCL;
    if (!valid_output(sda) || !valid_output(scl) || !valid_output(reset_pin) ||
        sda == scl || sda == reset_pin || scl == reset_pin) return ESP_ERR_INVALID_ARG;
    for (unsigned i=0; i<MO_SLOT_COUNT; i++) {
        int pin = enable_pin[i];
        if (pin == -1) continue;
        if (!valid_output(pin) || pin==sda || pin==scl || pin==reset_pin) return ESP_ERR_INVALID_ARG;
        for (unsigned j=0;j<i;j++) if (enable_pin[j]==pin) return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = gpio_set_direction(reset_pin, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) return err;
    if (!isolate()) return ESP_FAIL;
    i2c_master_bus_config_t cfg = {.i2c_port=-1, .sda_io_num=sda, .scl_io_num=scl,
        .clk_source=I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt=7};
    err = i2c_new_master_bus(&cfg, &bus);
    if (err != ESP_OK) return err;
    i2c_device_config_t dev = {.dev_addr_length=I2C_ADDR_BIT_LEN_7,
        .device_address=CONFIG_MOBAN_MUX_ADDRESS, .scl_speed_hz=100000};
    err = i2c_master_bus_add_device(bus, &dev, &mux);
    if (err != ESP_OK) goto cleanup;
    dev.device_address = LIMB_ADDRESS;
    err = i2c_master_bus_add_device(bus, &dev, &node);
    if (err != ESP_OK) goto cleanup;
    for (unsigned i=0; i<MO_SLOT_COUNT; i++) {
        if (enable_pin[i] < 0) continue;
        err = gpio_set_level(enable_pin[i], !SLOT_POWER_ON);
        if (err != ESP_OK) goto cleanup;
        err = gpio_set_direction(enable_pin[i], GPIO_MODE_OUTPUT);
        if (err != ESP_OK) goto cleanup;
        powered_off[i] = true;
        power_wait_ms[i] = (uint64_t)esp_timer_get_time()/1000 + 100;
    }
    session = esp_random();
    if (xTaskCreate(poll_task, "limb_bus", 4096, NULL, 5, NULL) == pdPASS) return ESP_OK;
    err = ESP_ERR_NO_MEM;
cleanup:
    if (node) i2c_master_bus_rm_device(node);
    if (mux) i2c_master_bus_rm_device(mux);
    i2c_del_master_bus(bus);
    node = NULL;
    mux = NULL;
    return err;
}
#else
esp_err_t limb_bus_start(void)
{
    ESP_LOGI("limb_bus", "Wiring not configured; slots remain offline");
    return ESP_OK;
}
#endif
