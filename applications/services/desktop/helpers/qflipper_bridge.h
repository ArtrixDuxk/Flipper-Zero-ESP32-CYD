#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Background qFlipper bridge.
 *
 * Installs the TinyUSB composite (VID/PID 0483:5740 — a real Flipper Zero) and
 * pipes a Flipper RPC session over CDC-ACM, so the desktop qFlipper app on a
 * host PC can talk to the device. Runs in its own FuriThread so the user stays
 * on the desktop while the bridge is active (toggled from the lock menu).
 *
 * ESP32-S3 / S2 use the USB-OTG CDC device. Classic ESP32 boards such as the
 * CYD use UART0 through their CH340 USB-to-UART adapter instead.
 */

/** Install the composite (idempotent — reuses an already-installed one) and
 *  start the bridge thread. A second call while active returns true. */
bool qflipper_bridge_start(void);

/** Stop the bridge thread and close the RPC session, but LEAVE the composite
 *  installed (like USB-Storage) — this esp_tinyusb build can't cleanly
 *  reinstall after an uninstall, so re-enabling just reattaches. The composite
 *  stays up until the next reboot. */
void qflipper_bridge_stop(void);

/** True while the bridge thread is running. */
bool qflipper_bridge_is_active(void);

/** True only while the bridge is passing binary RPC frames. Log sinks use this
 * to keep text diagnostics from corrupting the serial protobuf stream. */
bool qflipper_bridge_is_rpc_active(void);

/** Give qFlipper/WiFi reservations temporarily to a memory-intensive app.
 * begin() pauses screen frames and releases idle WiFi memory; resume() brings
 * screen frames back after app allocation. restore() is called after the app
 * is freed and rebuilds the protected WiFi reserve before resuming frames. */
bool qflipper_bridge_memory_handoff_begin(void);
void qflipper_bridge_memory_handoff_resume(void);
void qflipper_bridge_memory_handoff_restore(void);

#ifdef __cplusplus
}
#endif
