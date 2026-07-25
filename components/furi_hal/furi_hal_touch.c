/**
 * @file furi_hal_touch.c
 * Touch controller HAL.
 *
 * Backends (selected by board header):
 *   - CST816S / AXS5106L capacitive I2C (Waveshare C6 boards)
 *   - XPT2046 resistive bitbang SPI (ESP32-2432S028 / CYD)
 */

#include "furi_hal_touch.h"
#include "boards/board.h"

#include <furi.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_rom_sys.h>

#define TAG "FuriHalTouch"

static bool touch_initialized = false;
static volatile FuriThreadId touch_notify_thread = NULL;

/* ========================================================================== */
/* XPT2046 resistive touch (CYD)                                              */
/* ========================================================================== */
#ifdef BOARD_TOUCH_XPT2046

#ifndef BOARD_TOUCH_XPT_X_MIN
#define BOARD_TOUCH_XPT_X_MIN 200
#endif
#ifndef BOARD_TOUCH_XPT_X_MAX
#define BOARD_TOUCH_XPT_X_MAX 3800
#endif
#ifndef BOARD_TOUCH_XPT_Y_MIN
#define BOARD_TOUCH_XPT_Y_MIN 240
#endif
#ifndef BOARD_TOUCH_XPT_Y_MAX
#define BOARD_TOUCH_XPT_Y_MAX 3700
#endif
#ifndef BOARD_TOUCH_SWAP_XY
#define BOARD_TOUCH_SWAP_XY 0
#endif
#ifndef BOARD_TOUCH_INVERT_X
#define BOARD_TOUCH_INVERT_X 0
#endif
#ifndef BOARD_TOUCH_INVERT_Y
#define BOARD_TOUCH_INVERT_Y 0
#endif
#ifndef BOARD_TOUCH_Z_MIN
#define BOARD_TOUCH_Z_MIN 80
#endif

#define XPT_CMD_X  0xD0 /* measure X (12-bit, differential) */
#define XPT_CMD_Y  0x90 /* measure Y */
#define XPT_CMD_Z1 0xB0 /* pressure Z1 */

static void xpt_pin_out(gpio_num_t pin, int level) {
    gpio_set_level(pin, level);
}

static int xpt_pin_in(gpio_num_t pin) {
    return gpio_get_level(pin);
}

static void xpt_delay(void) {
    esp_rom_delay_us(1);
}

static uint16_t xpt_transfer16(uint8_t cmd) {
    const gpio_num_t clk = (gpio_num_t)BOARD_PIN_TOUCH_CLK;
    const gpio_num_t mosi = (gpio_num_t)BOARD_PIN_TOUCH_MOSI;
    const gpio_num_t miso = (gpio_num_t)BOARD_PIN_TOUCH_MISO;
    const gpio_num_t cs = (gpio_num_t)BOARD_PIN_TOUCH_CS;

    xpt_pin_out(cs, 0);
    xpt_delay();

    /* Clock out command (8 bits, MSB first) */
    for(int i = 7; i >= 0; i--) {
        xpt_pin_out(mosi, (cmd >> i) & 1);
        xpt_pin_out(clk, 1);
        xpt_delay();
        xpt_pin_out(clk, 0);
        xpt_delay();
    }

    /* Busy / sample gap */
    xpt_pin_out(clk, 1);
    xpt_delay();
    xpt_pin_out(clk, 0);
    xpt_delay();

    /* Read 12 data bits (MSB first), then 3 padding clocks */
    uint16_t value = 0;
    for(int i = 0; i < 12; i++) {
        xpt_pin_out(clk, 1);
        xpt_delay();
        value = (uint16_t)((value << 1) | (xpt_pin_in(miso) & 1));
        xpt_pin_out(clk, 0);
        xpt_delay();
    }
    for(int i = 0; i < 3; i++) {
        xpt_pin_out(clk, 1);
        xpt_delay();
        xpt_pin_out(clk, 0);
        xpt_delay();
    }

    xpt_pin_out(cs, 1);
    return value;
}

static uint16_t xpt_map(uint16_t raw, uint16_t raw_min, uint16_t raw_max, uint16_t out_max) {
    if(raw_max <= raw_min) return 0;
    if(raw < raw_min) raw = raw_min;
    if(raw > raw_max) raw = raw_max;
    return (uint16_t)(((uint32_t)(raw - raw_min) * out_max) / (raw_max - raw_min));
}

static uint16_t xpt_median3(uint16_t a, uint16_t b, uint16_t c) {
    if(a > b) {
        uint16_t t = a;
        a = b;
        b = t;
    }
    if(b > c) {
        uint16_t t = b;
        b = c;
        c = t;
    }
    if(a > b) {
        uint16_t t = a;
        a = b;
        b = t;
    }
    return b;
}

static bool xpt_read_raw(uint16_t* x_raw, uint16_t* y_raw, uint16_t* z_raw) {
    /* Triple sample + median: resistive SPI is noisy on CYD. */
    uint16_t xs[3], ys[3], zs[3];
    for(int i = 0; i < 3; i++) {
        xs[i] = xpt_transfer16(XPT_CMD_X);
        ys[i] = xpt_transfer16(XPT_CMD_Y);
        zs[i] = xpt_transfer16(XPT_CMD_Z1);
    }
    *x_raw = xpt_median3(xs[0], xs[1], xs[2]);
    *y_raw = xpt_median3(ys[0], ys[1], ys[2]);
    *z_raw = xpt_median3(zs[0], zs[1], zs[2]);
    return true;
}

void furi_hal_touch_init(void) {
    ESP_LOGI(TAG, "Initializing XPT2046 resistive touch");

    gpio_config_t out_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask =
            (1ULL << BOARD_PIN_TOUCH_CLK) | (1ULL << BOARD_PIN_TOUCH_MOSI) |
            (1ULL << BOARD_PIN_TOUCH_CS),
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);
    gpio_set_level((gpio_num_t)BOARD_PIN_TOUCH_CS, 1);
    gpio_set_level((gpio_num_t)BOARD_PIN_TOUCH_CLK, 0);
    gpio_set_level((gpio_num_t)BOARD_PIN_TOUCH_MOSI, 0);

    gpio_config_t in_cfg = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BOARD_PIN_TOUCH_MISO) | (1ULL << BOARD_PIN_TOUCH_IRQ),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    touch_initialized = true;
    ESP_LOGI(
        TAG,
        "XPT2046 ready (CLK=%d MOSI=%d MISO=%d CS=%d IRQ=%d)",
        BOARD_PIN_TOUCH_CLK,
        BOARD_PIN_TOUCH_MOSI,
        BOARD_PIN_TOUCH_MISO,
        BOARD_PIN_TOUCH_CS,
        BOARD_PIN_TOUCH_IRQ);
}

bool furi_hal_touch_int_active(void) {
    if(!touch_initialized) return false;
    /* IRQ is active-low while pressed */
    return gpio_get_level((gpio_num_t)BOARD_PIN_TOUCH_IRQ) == 0;
}

bool furi_hal_touch_is_pressed(void) {
    if(!touch_initialized) return false;
    if(furi_hal_touch_int_active()) return true;
    uint16_t x, y, z;
    if(!xpt_read_raw(&x, &y, &z)) return false;
    return z >= BOARD_TOUCH_Z_MIN;
}

void furi_hal_touch_get_xy(uint16_t* x, uint16_t* y) {
    *x = 0;
    *y = 0;
    TouchData data;
    if(furi_hal_touch_read(&data) && data.finger_count > 0) {
        *x = data.x;
        *y = data.y;
    }
}

bool furi_hal_touch_read(TouchData* data) {
    if(!touch_initialized || !data) {
        if(data) {
            data->gesture = TouchGestureNone;
            data->finger_count = 0;
            data->x = 0;
            data->y = 0;
        }
        return false;
    }

    data->gesture = TouchGestureNone;
    data->finger_count = 0;
    data->x = 0;
    data->y = 0;

    const bool irq_pressed = furi_hal_touch_int_active();
    uint16_t x_raw = 0, y_raw = 0, z_raw = 0;
    xpt_read_raw(&x_raw, &y_raw, &z_raw);

    /* Prefer pressure; IRQ alone can glitch on some CYD boards. */
    const bool pressed = (z_raw >= BOARD_TOUCH_Z_MIN) ||
                         (irq_pressed && z_raw >= (BOARD_TOUCH_Z_MIN / 2));
    if(!pressed) {
        return true;
    }

    /* Map raw ADC → landscape panel coordinates, then apply board transform so
     * (0,0) matches the top-left of the UI the user sees. */
    uint16_t ax = x_raw;
    uint16_t ay = y_raw;
#if BOARD_TOUCH_SWAP_XY
    ax = y_raw;
    ay = x_raw;
#endif

    uint16_t sx = xpt_map(ax, BOARD_TOUCH_XPT_X_MIN, BOARD_TOUCH_XPT_X_MAX, BOARD_LCD_H_RES - 1);
    uint16_t sy = xpt_map(ay, BOARD_TOUCH_XPT_Y_MIN, BOARD_TOUCH_XPT_Y_MAX, BOARD_LCD_V_RES - 1);

#if BOARD_TOUCH_INVERT_X
    sx = (uint16_t)((BOARD_LCD_H_RES - 1) - sx);
#endif
#if BOARD_TOUCH_INVERT_Y
    sy = (uint16_t)((BOARD_LCD_V_RES - 1) - sy);
#endif

    data->finger_count = 1;
    data->x = sx;
    data->y = sy;
    return true;
}

uint8_t furi_hal_touch_get_gesture(void) {
    return 0; /* software swipes in target_input */
}

void furi_hal_touch_set_notify_thread(void* thread_id) {
    touch_notify_thread = (FuriThreadId)thread_id;
}

#else /* !BOARD_TOUCH_XPT2046 — capacitive I2C path (CST816S / AXS5106L) */

#include <driver/i2c.h>

#define TOUCH_I2C_ADDR     BOARD_TOUCH_I2C_ADDR
#define TOUCH_SCL_PIN      BOARD_PIN_TOUCH_SCL
#define TOUCH_SDA_PIN      BOARD_PIN_TOUCH_SDA
#define TOUCH_I2C_PORT     BOARD_TOUCH_I2C_PORT
#define TOUCH_I2C_FREQ_HZ  BOARD_TOUCH_I2C_FREQ_HZ
#define TOUCH_I2C_TIMEOUT  BOARD_TOUCH_I2C_TIMEOUT

#ifdef BOARD_TOUCH_AXS5106L
#define TOUCH_CHIP_NAME    "AXS5106L"
#define TOUCH_DATA_REG     0x01
#define TOUCH_DATA_LEN     14
#define TOUCH_FINGER_IDX   1
#define TOUCH_XY_OFFSET    2
#define TOUCH_RST_LOW_MS   200
#define TOUCH_RST_HIGH_MS  300
#else
#define TOUCH_CHIP_NAME    "CST816S"
#define TOUCH_DATA_REG     0x00
#define TOUCH_DATA_LEN     7
#define TOUCH_FINGER_IDX   2
#define TOUCH_XY_OFFSET    3
#define TOUCH_RST_LOW_MS   10
#define TOUCH_RST_HIGH_MS  50
#endif

static bool touch_i2c_read(uint8_t reg, uint8_t* data, size_t len) {
#ifdef BOARD_TOUCH_AXS5106L
    if(i2c_master_write_to_device(
           TOUCH_I2C_PORT, TOUCH_I2C_ADDR, &reg, 1, TOUCH_I2C_TIMEOUT) != ESP_OK) {
        return false;
    }
    return i2c_master_read_from_device(
               TOUCH_I2C_PORT, TOUCH_I2C_ADDR, data, len, TOUCH_I2C_TIMEOUT) == ESP_OK;
#else
    esp_err_t err = i2c_master_write_read_device(
        TOUCH_I2C_PORT, TOUCH_I2C_ADDR, &reg, 1, data, len, TOUCH_I2C_TIMEOUT);
    return (err == ESP_OK);
#endif
}

#ifndef BOARD_TOUCH_AXS5106L
static bool touch_i2c_write(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    esp_err_t err = i2c_master_write_to_device(
        TOUCH_I2C_PORT, TOUCH_I2C_ADDR, buf, 2, TOUCH_I2C_TIMEOUT);
    return (err == ESP_OK);
}
#endif

static bool touch_read_point(uint8_t* finger_count, uint16_t* x, uint16_t* y) {
    uint8_t raw[TOUCH_DATA_LEN] = {0};
    if(!touch_i2c_read(TOUCH_DATA_REG, raw, TOUCH_DATA_LEN)) {
        return false;
    }
    *finger_count = raw[TOUCH_FINGER_IDX];
    *x = ((uint16_t)(raw[TOUCH_XY_OFFSET + 0] & 0x0F) << 8) | raw[TOUCH_XY_OFFSET + 1];
    *y = ((uint16_t)(raw[TOUCH_XY_OFFSET + 2] & 0x0F) << 8) | raw[TOUCH_XY_OFFSET + 3];
    return true;
}

void furi_hal_touch_init(void) {
    ESP_LOGI(TAG, "Initializing %s touch controller", TOUCH_CHIP_NAME);

#ifdef BOARD_PIN_TOUCH_RST
    if(BOARD_PIN_TOUCH_RST != UINT16_MAX) {
        gpio_set_direction((gpio_num_t)BOARD_PIN_TOUCH_RST, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)BOARD_PIN_TOUCH_RST, 0);
        furi_delay_ms(TOUCH_RST_LOW_MS);
        gpio_set_level((gpio_num_t)BOARD_PIN_TOUCH_RST, 1);
        furi_delay_ms(TOUCH_RST_HIGH_MS);
    }
#endif

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_SDA_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = TOUCH_SCL_PIN,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = TOUCH_I2C_FREQ_HZ,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(TOUCH_I2C_PORT, &conf);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
        return;
    }

    err = i2c_driver_install(TOUCH_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return;
    }

#ifdef BOARD_TOUCH_AXS5106L
    uint8_t id[2] = {0};
    if(touch_i2c_read(0x08, id, 2)) {
        ESP_LOGI(TAG, "AXS5106L device ID: 0x%02X%02X", id[0], id[1]);
    } else {
        ESP_LOGW(TAG, "AXS5106L not responding (addr 0x%02X)", TOUCH_I2C_ADDR);
    }
#else
    if(touch_i2c_write(0x00, 0x00)) {
        ESP_LOGI(TAG, "CST816S set to normal mode");
    } else {
        ESP_LOGW(TAG, "CST816S not responding (may wake on first touch)");
    }

    if(touch_i2c_write(0xFE, 0x01)) {
        ESP_LOGI(TAG, "CST816S auto-sleep disabled");
    } else {
        ESP_LOGW(TAG, "CST816S auto-sleep disable failed");
    }

    uint8_t chip_id = 0;
    if(touch_i2c_read(0xA7, &chip_id, 1)) {
        ESP_LOGI(TAG, "CST816S chip ID: 0x%02X", chip_id);
    }
#endif

    touch_initialized = true;
    ESP_LOGI(TAG, "Touch init OK (I2C polling mode)");
}

bool furi_hal_touch_is_pressed(void) {
    if(!touch_initialized) return false;

    uint8_t finger_count = 0;
    uint16_t x = 0, y = 0;
    if(!touch_read_point(&finger_count, &x, &y)) {
        return false;
    }
    return (finger_count > 0);
}

void furi_hal_touch_get_xy(uint16_t* x, uint16_t* y) {
    *x = 0;
    *y = 0;
    if(!touch_initialized) return;

    uint8_t finger_count = 0;
    touch_read_point(&finger_count, x, y);
}

bool furi_hal_touch_read(TouchData* data) {
    if(!touch_initialized || !data) {
        if(data) {
            data->gesture = TouchGestureNone;
            data->finger_count = 0;
            data->x = 0;
            data->y = 0;
        }
        return false;
    }

    uint8_t raw[TOUCH_DATA_LEN] = {0};
    if(!touch_i2c_read(TOUCH_DATA_REG, raw, TOUCH_DATA_LEN)) {
        data->gesture = TouchGestureNone;
        data->finger_count = 0;
        data->x = 0;
        data->y = 0;
        return false;
    }

#ifdef BOARD_TOUCH_AXS5106L
    data->gesture = TouchGestureNone;
#else
    data->gesture = (TouchGesture)raw[1];
#endif
    data->finger_count = raw[TOUCH_FINGER_IDX];
    data->x = ((uint16_t)(raw[TOUCH_XY_OFFSET + 0] & 0x0F) << 8) | raw[TOUCH_XY_OFFSET + 1];
    data->y = ((uint16_t)(raw[TOUCH_XY_OFFSET + 2] & 0x0F) << 8) | raw[TOUCH_XY_OFFSET + 3];
    return true;
}

uint8_t furi_hal_touch_get_gesture(void) {
    if(!touch_initialized) return 0;

#ifdef BOARD_TOUCH_AXS5106L
    return 0;
#else
    uint8_t data[TOUCH_DATA_LEN] = {0};
    if(!touch_i2c_read(TOUCH_DATA_REG, data, TOUCH_DATA_LEN)) return 0;
    return data[1];
#endif
}

bool furi_hal_touch_int_active(void) {
    return false;
}

void furi_hal_touch_set_notify_thread(void* thread_id) {
    touch_notify_thread = (FuriThreadId)thread_id;
}

#endif /* BOARD_TOUCH_XPT2046 */
