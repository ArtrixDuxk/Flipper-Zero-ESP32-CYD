#include "furi_hal.h"
#include "furi_hal_sd.h"
#include "boards/board.h"
#include <furi_hal_gpio.h>
#include <esp_log.h>
#include <nvs_flash.h>

static const char* TAG = "FuriHal";

void furi_hal_init_early(void) {
    furi_hal_cortex_init_early();

#if defined(BOARD_HAS_SD_CARD) && BOARD_HAS_SD_CARD && defined(BOARD_PIN_SD_CS) && \
    (BOARD_PIN_SD_CS != UINT16_MAX)
    /* CYD shares the SD clock/data lines with the RF header.  Keep the card
     * deselected before the early CC1101 probe; otherwise those clocks can be
     * interpreted as a partial SD command and make the first mount/write
     * intermittently fail until a full power cycle. */
    static const GpioPin sd_csn = {.port = NULL, .pin = BOARD_PIN_SD_CS};
    furi_hal_gpio_init_simple(&sd_csn, GpioModeOutputPushPull);
    furi_hal_gpio_write(&sd_csn, true);
    ESP_LOGI(TAG, "SD_CSN GPIO%d set HIGH (deselect)", BOARD_PIN_SD_CS);
#endif

#ifdef BOARD_PIN_PWR_EN
    /* Power-enable must be set early — powers CC1101, BQ27220 fuel gauge, WS2812 */
    static const GpioPin pwr_en = {.port = NULL, .pin = BOARD_PIN_PWR_EN};
    furi_hal_gpio_init_simple(&pwr_en, GpioModeOutputPushPull);
    furi_hal_gpio_write(&pwr_en, true);
    ESP_LOGI(TAG, "PWR_EN GPIO%d set HIGH", BOARD_PIN_PWR_EN);
#endif

#ifdef BOARD_PIN_NRF24_CSN
    /* Deselect nRF24 CS before any SPI. On NM-RF-HAT this pin is also CC1101 CSN
     * (DIP-muxed) — driving it HIGH is correct for both (idle/deselected). */
    static const GpioPin nrf24_csn = {.port = NULL, .pin = BOARD_PIN_NRF24_CSN};
    furi_hal_gpio_init_simple(&nrf24_csn, GpioModeOutputPushPull);
    furi_hal_gpio_write(&nrf24_csn, true);
    ESP_LOGI(TAG, "NRF24_CSN GPIO%d set HIGH (deselect)", BOARD_PIN_NRF24_CSN);
#endif

#if defined(BOARD_PIN_NRF24_CE) && \
    !(defined(BOARD_RF_MUX_SHARED_CTRL) && BOARD_RF_MUX_SHARED_CTRL && \
      defined(BOARD_PIN_CC1101_GDO0) && (BOARD_PIN_NRF24_CE == BOARD_PIN_CC1101_GDO0))
    /* Only force CE low when it is a dedicated nRF24 pin. On the HAT, CE shares
     * GPIO with CC1101 GDO0 — that line must stay an input for SubGHz. */
    static const GpioPin nrf24_ce = {.port = NULL, .pin = BOARD_PIN_NRF24_CE};
    furi_hal_gpio_init_simple(&nrf24_ce, GpioModeOutputPushPull);
    furi_hal_gpio_write(&nrf24_ce, false);
    ESP_LOGI(TAG, "NRF24_CE GPIO%d set LOW (standby)", BOARD_PIN_NRF24_CE);
#elif defined(BOARD_PIN_CC1101_GDO0) && (BOARD_PIN_CC1101_GDO0 != UINT16_MAX)
    /* Ensure GDO0 is input before CC1101 probe */
    static const GpioPin gdo0 = {.port = NULL, .pin = BOARD_PIN_CC1101_GDO0};
    furi_hal_gpio_init_simple(&gdo0, GpioModeInput);
#endif

    ESP_LOGI(TAG, "Early init complete");
}

void furi_hal_deinit_early(void) {
}

void furi_hal_init(void) {
    /* NVS is required by WiFi and BLE — init once at boot */
    esp_err_t nvs_err = nvs_flash_init();
    if(nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    furi_hal_rtc_init();
    furi_hal_version_init();
    furi_hal_power_init();
    furi_hal_crypto_init();
#if defined(BOARD_HAS_SD_CARD) && BOARD_HAS_SD_CARD
    /* On CYD the SD card and RF modules share SPI3. Initialize the SD device
     * first so the subsequent CC1101 probe cannot clock an uninitialized,
     * potentially selected card into an undefined protocol state. The
     * storage service will mount FatFS later. */
    furi_hal_sd_presence_init();
    if(furi_hal_sd_init(false) != FuriStatusOk) {
        ESP_LOGW(TAG, "Early SD init failed; storage service will retry");
    }
#endif
    furi_hal_subghz_init();
    furi_hal_usb_init();
    furi_hal_light_init();
    furi_hal_display_init();
    furi_hal_speaker_init();
    furi_hal_nfc_init();
    ESP_LOGI(TAG, "Init complete");
}
