/**
 * @file board_esp32_cyd_nm_rf_hat.h
 * Board definition: ESP32-2432S028 (CYD classic) + NM-RF-HAT
 *
 * MCU:      ESP32-WROOM-32 (dual-core Xtensa LX6, typically 4 MB flash, no PSRAM)
 * Display:  ILI9341 320×240 RGB565 via HSPI (SPI2)
 * Touch:    XPT2046 resistive (bitbang SPI)
 * SD Card:  VSPI (SPI3) — shared with CC1101 + nRF24 on the HAT
 * SubGHz:   CC1101 on NM-RF-HAT (DIP pos 1)
 * nRF24:    nRF24L01 on NM-RF-HAT (DIP pos 2)
 * NFC:      PN532 I2C on NM-RF-HAT (DIP pos 3) — SDA/SCL = IO27/IO22
 * IR:       TX/RX on NM-RF-HAT (DIP pos 4) — TX=IO22 RX=IO27
 * RF433:    OOK on NM-RF-HAT (DIP pos 5) — DT=IO22 DR=IO27
 *
 * IMPORTANT: The HAT 6-position DIP enables only ONE RF function at a time.
 * IO22/IO27 are multiplexed across CC1101 / nRF24 / PN532 / IR / RF433.
 *
 * Pin map from NM-RF-HAT schematic v1.0 + user manual (ESP32-2432S028 wiring).
 */

#pragma once

/* ---- Board metadata ---- */
#define BOARD_NAME        "ESP32-CYD + NM-RF-HAT"
#define BOARD_TARGET      "esp32"

/* ---- LCD driver selection (consumed by furi_hal_display.c) ---- */
#define BOARD_LCD_DRIVER_ILI9341 1

/* ---- Hardware Button ---- */
#define BOARD_PIN_BUTTON_BOOT   0   /* BOOT button (active low) */
#define BOARD_PIN_BATTERY_ADC   UINT16_MAX  /* HAT battery path TBD; no on-board ADC map */

/* ---- LCD Pins (ILI9341 via HSPI / SPI2) ---- */
#define BOARD_PIN_LCD_MOSI      13
#define BOARD_PIN_LCD_SCLK      14
#define BOARD_PIN_LCD_DC        2
#define BOARD_PIN_LCD_CS        15
#define BOARD_PIN_LCD_RST       UINT16_MAX  /* Not broken out on most CYDs (tied/EN) */
#define BOARD_PIN_LCD_BL        21
#define BOARD_PIN_LCD_MISO      12          /* TFT SDO — separate from SD/RF bus */

/* ---- LCD Display Configuration ---- */
#define BOARD_LCD_H_RES         320
#define BOARD_LCD_V_RES         240
#define BOARD_LCD_SPI_HOST      SPI2_HOST   /* HSPI — LCD only */
#define BOARD_LCD_SPI_FREQ_HZ   (40 * 1000 * 1000)
#define BOARD_LCD_CMD_BITS      8
#define BOARD_LCD_PARAM_BITS    8
#define BOARD_LCD_SWAP_XY       true        /* landscape via MV bit */
/* Leave esp_lcd mirror helpers off — we force MADCTL after init (TFT_eSPI style).
 * 0x68 = MX | MV | BGR  → landscape, USB-port orientation used by most CYDs.
 * If still mirrored, try 0xA8 (MY|MV|BGR) or 0x28 (MV|BGR) via rebuild. */
#define BOARD_LCD_MIRROR_X      false
#define BOARD_LCD_MIRROR_Y      false
#define BOARD_LCD_INVERT_COLOR  false
#define BOARD_LCD_GAP_X         0
#define BOARD_LCD_GAP_Y         0
#define BOARD_LCD_BL_ACTIVE_LOW false
#define BOARD_LCD_COLOR_ORDER_BGR true
#define BOARD_LCD_MADCTL        0x68
/* Software L↔R of the scaled UI inside a FULL-WIDTH stripe (avoids MX + partial-rect bugs). */
#define BOARD_LCD_FB_MIRROR_X   1
#define BOARD_LCD_FB_MIRROR_Y   0

/* Flipper mono → RGB565 (byte-swapped for SPI). Tune if orange looks wrong. */
#define BOARD_LCD_FG_COLOR      0x20FD      /* Orange-ish */
#define BOARD_LCD_BG_COLOR      0x0000      /* Black */

/* ---- SD Card (VSPI / SPI3, shared with CC1101 + nRF24) ---- */
#define BOARD_PIN_SD_CS         5
#define BOARD_PIN_SD_MISO       19
#define BOARD_PIN_SD_MOSI       23
#define BOARD_PIN_SD_SCK        18

/* RF + SD SPI host (separate from LCD HSPI) */
#define BOARD_RF_SPI_HOST       SPI3_HOST

/* ---- Touch Controller (XPT2046 resistive, bitbang SPI) ---- */
#define BOARD_TOUCH_XPT2046     1
#define BOARD_PIN_TOUCH_CLK     25
#define BOARD_PIN_TOUCH_MOSI    32
#define BOARD_PIN_TOUCH_MISO    39
#define BOARD_PIN_TOUCH_CS      33
#define BOARD_PIN_TOUCH_IRQ     36
/* Unused I2C touch placeholders (required by resources.c / board.h sanity) */
#define BOARD_PIN_TOUCH_SCL     UINT16_MAX
#define BOARD_PIN_TOUCH_SDA     UINT16_MAX
#define BOARD_PIN_TOUCH_RST     UINT16_MAX
#define BOARD_PIN_TOUCH_INT     BOARD_PIN_TOUCH_IRQ
#define BOARD_TOUCH_I2C_ADDR    0x00
#define BOARD_TOUCH_I2C_PORT    I2C_NUM_0
#define BOARD_TOUCH_I2C_FREQ_HZ 0
#define BOARD_TOUCH_I2C_TIMEOUT 0
/* Raw ADC calibration (typical CYD XPT2046). After MADCTL landscape + FB mirror
 * the logical axes need swap/invert to match what you see on screen.
 * If Left/Right feel swapped: flip BOARD_TOUCH_INVERT_X.
 * If Up/Down feel swapped: flip BOARD_TOUCH_INVERT_Y. */
#define BOARD_TOUCH_XPT_X_MIN   200
#define BOARD_TOUCH_XPT_X_MAX   3800
#define BOARD_TOUCH_XPT_Y_MIN   240
#define BOARD_TOUCH_XPT_Y_MAX   3700
#define BOARD_TOUCH_SWAP_XY     1   /* XPT axes vs panel landscape */
/* FB is mirrored in software; resistive X usually already matches visual left.
 * INVERT_X=1 made left-edge "Back" fire Ok and trapped users in apps (e.g. NFC). */
#define BOARD_TOUCH_INVERT_X    0
#define BOARD_TOUCH_INVERT_Y    0
#define BOARD_TOUCH_Z_MIN       100 /* fewer ghost presses on resistive */

/* ---- SubGHz / CC1101 (NM-RF-HAT DIP pos 1) ---- */
#define BOARD_PIN_CC1101_SCK    18
#define BOARD_PIN_CC1101_CSN    27          /* multiplexed control pair with GDO0 */
#define BOARD_PIN_CC1101_MISO   19
#define BOARD_PIN_CC1101_MOSI   23
#define BOARD_PIN_CC1101_GDO0   22
#define BOARD_PIN_CC1101_SW0    UINT16_MAX  /* No band-switch GPIOs on HAT */
#define BOARD_PIN_CC1101_SW1    UINT16_MAX
#define BOARD_CC1101_SPI_SHARED 1           /* Shares VSPI with SD + nRF24 */

/* ---- nRF24L01 (NM-RF-HAT DIP pos 2) ---- */
#define BOARD_PIN_NRF24_SCK     BOARD_PIN_CC1101_SCK
#define BOARD_PIN_NRF24_MISO    BOARD_PIN_CC1101_MISO
#define BOARD_PIN_NRF24_MOSI    BOARD_PIN_CC1101_MOSI
#define BOARD_PIN_NRF24_CSN     27
#define BOARD_PIN_NRF24_CE      22
#define BOARD_HAS_NRF24         1

/* ---- NFC / PN532 I2C (NM-RF-HAT DIP pos 3) ---- */
#define BOARD_PIN_NFC_SCL       22
#define BOARD_PIN_NFC_SDA       27
#define BOARD_PIN_NFC_IRQ       UINT16_MAX
#define BOARD_PIN_NFC_RST       UINT16_MAX
#define BOARD_NFC_I2C_PORT      I2C_NUM_0
/* HAT DIP mux: same wires as CC1101 GDO0/CSN and nRF24 CE/CSN.
 * Never grab I2C on these pins at boot — only when NFC app runs. */
#define BOARD_NFC_LAZY_INIT     1
#define BOARD_RF_MUX_SHARED_CTRL 1

/* ---- IR (NM-RF-HAT DIP pos 4) ---- */
#define BOARD_PIN_IR_TX         22
#define BOARD_PIN_IR_RX         27

/* ---- RGB LED (on CYD, active-low; optional status) ---- */
#define BOARD_PIN_LED_R         4
#define BOARD_PIN_LED_G         16
#define BOARD_PIN_LED_B         17

/* ---- Features ---- */
#define BOARD_HAS_TOUCH         1
#define BOARD_HAS_ENCODER       0
#define BOARD_HAS_SD_CARD       1
#define BOARD_HAS_BLE           1
#define BOARD_HAS_RGB_LED       0           /* discrete RGB, not WS2812 path */
#define BOARD_HAS_VIBRO         0
#define BOARD_HAS_SPEAKER       0           /* IO26 amp exists; I2S path not wired yet */
#define BOARD_HAS_IR            1
#define BOARD_HAS_IBUTTON       0
#define BOARD_HAS_RFID          0           /* 125 kHz not on HAT */
#define BOARD_HAS_NFC           1
#define BOARD_HAS_SUBGHZ        1
#define BOARD_HAS_MIC           0

/* Power virtual capacity (HAT 18650 / LiPo; gauge not yet hooked) */
#define BQ27220_ADDR                    0x55
#define BQ25896_CHARGE_LIMIT            1280
#define FURI_HAL_POWER_VIRTUAL_CAPACITY_MAH (2000U)
