/**
 * @file furi_hal_rf_mux.h
 * NM-RF-HAT DIP mux: IO22/IO27 are shared across CC1101 / nRF24 / PN532 / IR.
 * Only one path may own those pins at a time. Call claim() before probing.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FuriHalRfMuxPathIdle = 0,
    FuriHalRfMuxPathSubGhz,
    FuriHalRfMuxPathNrf24,
    FuriHalRfMuxPathNfc,
    FuriHalRfMuxPathIr,
} FuriHalRfMuxPath;

/**
 * Reconfigure the shared control pins for the given RF path.
 * No-op on boards without BOARD_RF_MUX_SHARED_CTRL.
 */
void furi_hal_rf_mux_claim(FuriHalRfMuxPath path);

/** Current claimed path (Idle if none / non-mux board). */
FuriHalRfMuxPath furi_hal_rf_mux_current(void);

/**
 * True when this board multiplexes RF control pins (DIP HAT).
 */
bool furi_hal_rf_mux_is_shared(void);

#ifdef __cplusplus
}
#endif
