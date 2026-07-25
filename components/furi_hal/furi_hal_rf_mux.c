/**
 * @file furi_hal_rf_mux.c
 * Claim / release IO22+IO27 for NM-RF-HAT DIP-muxed RF peripherals.
 */

#include "furi_hal_rf_mux.h"
#include "furi_hal_nfc.h"
#include "boards/board.h"

#include <driver/gpio.h>
#include <esp_log.h>

static const char* TAG = "RfMux";

#if defined(BOARD_RF_MUX_SHARED_CTRL) && BOARD_RF_MUX_SHARED_CTRL

#ifndef BOARD_PIN_RF_MUX_A
/* Default HAT control pair (GDO0/CE/SCL/IR_TX , CSN/SDA/IR_RX) */
#define BOARD_PIN_RF_MUX_A BOARD_PIN_CC1101_GDO0
#define BOARD_PIN_RF_MUX_B BOARD_PIN_CC1101_CSN
#endif

static FuriHalRfMuxPath s_path = FuriHalRfMuxPathIdle;

static void rf_mux_reset_pins(void) {
    if(BOARD_PIN_RF_MUX_A != UINT16_MAX) {
        gpio_reset_pin((gpio_num_t)BOARD_PIN_RF_MUX_A);
    }
    if(BOARD_PIN_RF_MUX_B != UINT16_MAX) {
        gpio_reset_pin((gpio_num_t)BOARD_PIN_RF_MUX_B);
    }
}

static void rf_mux_leave(FuriHalRfMuxPath prev) {
    switch(prev) {
    case FuriHalRfMuxPathNfc:
        /* Drop I2C so pins can become SPI CS / RMT again */
        furi_hal_nfc_bus_force_release();
        break;
    case FuriHalRfMuxPathSubGhz:
    case FuriHalRfMuxPathNrf24:
    case FuriHalRfMuxPathIr:
    case FuriHalRfMuxPathIdle:
    default:
        break;
    }
    rf_mux_reset_pins();
}

static void rf_mux_enter(FuriHalRfMuxPath path) {
    switch(path) {
    case FuriHalRfMuxPathSubGhz:
        /* CSN idle high, GDO0 input (SPI device uses software CS) */
        if(BOARD_PIN_RF_MUX_B != UINT16_MAX) {
            gpio_set_direction((gpio_num_t)BOARD_PIN_RF_MUX_B, GPIO_MODE_OUTPUT);
            gpio_set_level((gpio_num_t)BOARD_PIN_RF_MUX_B, 1);
        }
        if(BOARD_PIN_RF_MUX_A != UINT16_MAX) {
            gpio_set_direction((gpio_num_t)BOARD_PIN_RF_MUX_A, GPIO_MODE_INPUT);
            gpio_set_pull_mode((gpio_num_t)BOARD_PIN_RF_MUX_A, GPIO_FLOATING);
        }
        ESP_LOGI(TAG, "Claim SubGHz (CSN=GPIO%u GDO0=GPIO%u) — DIP pos 1",
                 (unsigned)BOARD_PIN_RF_MUX_B,
                 (unsigned)BOARD_PIN_RF_MUX_A);
        break;

    case FuriHalRfMuxPathNrf24:
        /* CSN high, CE low until radio powers up */
        if(BOARD_PIN_RF_MUX_B != UINT16_MAX) {
            gpio_set_direction((gpio_num_t)BOARD_PIN_RF_MUX_B, GPIO_MODE_OUTPUT);
            gpio_set_level((gpio_num_t)BOARD_PIN_RF_MUX_B, 1);
        }
        if(BOARD_PIN_RF_MUX_A != UINT16_MAX) {
            gpio_set_direction((gpio_num_t)BOARD_PIN_RF_MUX_A, GPIO_MODE_OUTPUT);
            gpio_set_level((gpio_num_t)BOARD_PIN_RF_MUX_A, 0);
        }
        ESP_LOGI(TAG, "Claim nRF24 (CSN=GPIO%u CE=GPIO%u) — DIP pos 2",
                 (unsigned)BOARD_PIN_RF_MUX_B,
                 (unsigned)BOARD_PIN_RF_MUX_A);
        break;

    case FuriHalRfMuxPathNfc:
        /* I2C driver will reconfigure SDA/SCL; leave pins released */
        ESP_LOGI(TAG, "Claim NFC (SDA=GPIO%u SCL=GPIO%u) — DIP pos 3",
                 (unsigned)BOARD_PIN_NFC_SDA,
                 (unsigned)BOARD_PIN_NFC_SCL);
        break;

    case FuriHalRfMuxPathIr:
        /* RMT will take TX/RX GPIOs */
        ESP_LOGI(TAG, "Claim IR (TX=GPIO%u RX=GPIO%u) — DIP pos 4",
                 (unsigned)BOARD_PIN_IR_TX,
                 (unsigned)BOARD_PIN_IR_RX);
        break;

    case FuriHalRfMuxPathIdle:
    default:
        ESP_LOGI(TAG, "RF mux idle");
        break;
    }
}

void furi_hal_rf_mux_claim(FuriHalRfMuxPath path) {
    if(s_path == path) {
        /* Re-assert pin modes in case another subsystem touched them */
        rf_mux_enter(path);
        return;
    }
    rf_mux_leave(s_path);
    s_path = path;
    rf_mux_enter(path);
}

FuriHalRfMuxPath furi_hal_rf_mux_current(void) {
    return s_path;
}

bool furi_hal_rf_mux_is_shared(void) {
    return true;
}

#else /* !BOARD_RF_MUX_SHARED_CTRL */

void furi_hal_rf_mux_claim(FuriHalRfMuxPath path) {
    (void)path;
}

FuriHalRfMuxPath furi_hal_rf_mux_current(void) {
    return FuriHalRfMuxPathIdle;
}

bool furi_hal_rf_mux_is_shared(void) {
    return false;
}

#endif
