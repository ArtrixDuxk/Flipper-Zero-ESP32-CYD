#include <furi.h>
#include <furi_hal.h>
#include <flipper.h>
#include <applications.h>

#include "applications/services/desktop/helpers/qflipper_bridge.h"
#include "applications/main/wlan_app/wlan_hal.h"

#include <esp_log.h>
#include <esp_rom_uart.h>

#include <stdarg.h>

static const char* TAG = "Main";
static vprintf_like_t s_esp_log_vprintf = NULL;

/* UART0 is also the CYD qFlipper transport. ESP_LOG writes textual diagnostics
 * directly to it, which would corrupt protobuf frames; keep diagnostics on
 * until the bridge enters binary RPC mode, then drop them. */
static int qflipper_aware_esp_log_vprintf(const char* format, va_list args) {
    if(qflipper_bridge_is_rpc_active()) return 0;
    return s_esp_log_vprintf ? s_esp_log_vprintf(format, args) : 0;
}

static void log_internal_registry(
    const char* label,
    const FlipperInternalApplication* apps,
    size_t count) {
    if(count == 0) {
        ESP_LOGI(TAG, "%s: none", label);
        return;
    }

    for(size_t i = 0; i < count; ++i) {
        ESP_LOGI(
            TAG,
            "%s[%u]: appid=%s name=%s stack=%lu",
            label,
            (unsigned)i,
            apps[i].appid ? apps[i].appid : "(null)",
            apps[i].name ? apps[i].name : "(null)",
            (unsigned long)apps[i].stack_size);
    }
}

static void log_external_registry(
    const char* label,
    const FlipperExternalApplication* apps,
    size_t count) {
    if(count == 0) {
        ESP_LOGI(TAG, "%s: none", label);
        return;
    }

    for(size_t i = 0; i < count; ++i) {
        ESP_LOGI(
            TAG,
            "%s[%u]: name=%s launch=%s",
            label,
            (unsigned)i,
            apps[i].name ? apps[i].name : "(null)",
            apps[i].path ? apps[i].path : "(null)");
    }
}

static bool registry_contains_appid(
    const FlipperInternalApplication* apps,
    size_t count,
    const char* appid) {
    for(size_t i = 0; i < count; ++i) {
        if(apps[i].appid && strcmp(apps[i].appid, appid) == 0) {
            return true;
        }
    }

    return false;
}

static bool registry_contains_launch(
    const FlipperExternalApplication* apps,
    size_t count,
    const char* launch) {
    for(size_t i = 0; i < count; ++i) {
        if(apps[i].path && strcmp(apps[i].path, launch) == 0) {
            return true;
        }
    }

    return false;
}

static void log_registry_snapshot(void) {
    ESP_LOGI(
        TAG,
        "Registry counts: services=%u startup=%u apps=%u settings=%u extsettings=%u external=%u internal_external=%u system=%u debug=%u",
        (unsigned)FLIPPER_SERVICES_COUNT,
        (unsigned)FLIPPER_ON_SYSTEM_START_COUNT,
        (unsigned)FLIPPER_APPS_COUNT,
        (unsigned)FLIPPER_SETTINGS_APPS_COUNT,
        (unsigned)FLIPPER_EXTSETTINGS_APPS_COUNT,
        (unsigned)FLIPPER_EXTERNAL_APPS_COUNT,
        (unsigned)FLIPPER_INTERNAL_EXTERNAL_APPS_COUNT,
        (unsigned)FLIPPER_SYSTEM_APPS_COUNT,
        (unsigned)FLIPPER_DEBUG_APPS_COUNT);

    log_internal_registry("service", FLIPPER_SERVICES, FLIPPER_SERVICES_COUNT);
    log_internal_registry("settings", FLIPPER_SETTINGS_APPS, FLIPPER_SETTINGS_APPS_COUNT);
    log_external_registry("extsettings", FLIPPER_EXTSETTINGS_APPS, FLIPPER_EXTSETTINGS_APPS_COUNT);

    if(!registry_contains_appid(FLIPPER_SERVICES, FLIPPER_SERVICES_COUNT, "bt")) {
        ESP_LOGW(TAG, "Bluetooth service appid 'bt' is not present in FLIPPER_SERVICES");
    }

    if(!registry_contains_appid(FLIPPER_SETTINGS_APPS, FLIPPER_SETTINGS_APPS_COUNT, "bt_settings") &&
       !registry_contains_launch(
           FLIPPER_EXTSETTINGS_APPS, FLIPPER_EXTSETTINGS_APPS_COUNT, "bt_settings")) {
        ESP_LOGW(
            TAG,
            "Bluetooth settings app 'bt_settings' is not present in settings or extsettings registry");
    }
}

static void furi_log_esp_callback(const uint8_t* data, size_t size, void* context) {
    (void)context;
    if(qflipper_bridge_is_rpc_active()) return;
    for(size_t i = 0; i < size; ++i) {
        esp_rom_output_putc((char)data[i]);
    }
}

void app_main(void) {
    s_esp_log_vprintf = esp_log_set_vprintf(qflipper_aware_esp_log_vprintf);
    ESP_LOGI(TAG, "Starting Furi Core on ESP32...");

    // ESP-IDF: Scheduler is already running!
    furi_init();

    /* Keep a contiguous block available for WiFi before qFlipper can start
     * its screen-stream worker and fragment the internal heap. */
    wlan_hal_reserve_memory();

    // Register ESP32 log handler
    FuriLogHandler log_handler = {
        .callback = furi_log_esp_callback,
        .context = NULL,
    };
    furi_log_add_handler(log_handler);

    furi_hal_init_early();
    furi_hal_init();
    flipper_init();
    log_registry_snapshot();

    for(size_t i = 0; i < FLIPPER_SERVICES_COUNT; i++) {
        FuriThread* thread = furi_thread_alloc_service(
            FLIPPER_SERVICES[i].name,
            FLIPPER_SERVICES[i].stack_size,
            FLIPPER_SERVICES[i].app,
            NULL);
        furi_thread_set_appid(thread, FLIPPER_SERVICES[i].appid);
        furi_thread_start(thread);

        furi_delay_ms(10);
    }

    for(size_t i = 0; i < FLIPPER_ON_SYSTEM_START_COUNT; i++) {
        FLIPPER_ON_SYSTEM_START[i]();
    }

    ESP_LOGI(TAG, "All services started, entering background...");

    // This blocks forever (thread scrubber)
    furi_background();
}
