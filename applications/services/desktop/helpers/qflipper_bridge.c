/**
 * Background qFlipper bridge — see qflipper_bridge.h.
 *
 * This is the former standalone "qFlipper" app (applications/main/qflipper)
 * turned into a background service owned by the desktop: the composite + RPC
 * pipe run in a dedicated FuriThread instead of a foreground view, so the lock
 * menu can toggle it on/off while the user keeps using the desktop.
 *
 * Installing the TinyUSB composite switches the ESP32-S3 internal USB PHY from
 * USB-Serial-JTAG to USB-OTG, which kills the serial/JTAG bridge esptool uses
 * for flashing. So we only do it on demand. stop() leaves the composite
 * installed (like USB-Storage) and only detaches the RPC bridge — this
 * esp_tinyusb build can't cleanly reinstall after an uninstall, so the
 * composite stays up until reboot.
 */

#include "qflipper_bridge.h"
#include "applications/main/wlan_app/wlan_app.h"
#include "applications/main/wlan_app/wlan_hal.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_cdc.h>
#include <rpc/rpc.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32
#include <driver/uart.h>
#include <esp_rom_sys.h>
#include <esp_rom_uart.h>
#define QFLIPPER_HAVE_UART 1
#else
#define QFLIPPER_HAVE_UART 0
#endif

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2
#include "furi_hal_usb_tinyusb_composite.h"
#define QFLIPPER_HAVE_COMPOSITE 1
#else
#define QFLIPPER_HAVE_COMPOSITE 0
#endif

#define TAG "qFlipperBridge"

#define USB_RPC_CDC_ITF       0
/* Larger drain chunks shorten the race window between the avail-check and
 * the FIFO read in usb_rpc_drain_rx — fewer iterations, fewer chances for
 * the host to slip a byte into the FIFO that we'd then drop. */
#define USB_RPC_RX_CHUNK_SIZE 2048

/* Internal events */
typedef enum {
    UsbRpcEventConnected,    /* DTR went high */
    UsbRpcEventDisconnected, /* DTR went low or USB disconnect */
    UsbRpcEventRxAvailable,  /* CDC RX bytes ready */
    UsbRpcEventTxComplete,   /* CDC TX done */
    UsbRpcEventExit,         /* stop() requested — leave the loop */
} UsbRpcEvent;

typedef struct {
    FuriThread* thread;
    FuriMessageQueue* event_q;

    Rpc* rpc;
    RpcSession* session;

    bool connected;       /* DTR state */
    bool session_open;    /* RPC session active */
    bool rpc_mode;        /* false = CLI handshake, true = piping protobuf */
    volatile bool exiting; /* stop() requested — bail out of any blocking loop */

    /* CLI handshake line buffer (qFlipper sends "start_rpc_session\r") */
    char cli_buf[64];
    size_t cli_len;

    /* RX scratch buffer */
    uint8_t rx_buf[USB_RPC_RX_CHUNK_SIZE];
} UsbRpcSrv;

/* Single-instance background bridge. */
static UsbRpcSrv* s_bridge = NULL;

#if QFLIPPER_HAVE_UART

/* The CYD's USB connector terminates in a CH340, so UART0 is the only PC
 * transport.  Keep this small: the RPC service already owns framing and all
 * qFlipper protocol handling. */
typedef struct {
    FuriThread* thread;
    Rpc* rpc;
    RpcSession* session;
    volatile bool exiting;
    volatile bool rpc_mode;
    volatile bool close_requested;
    volatile bool session_terminated;
    bool reopen_requested;
    char cli_buf[64];
    size_t cli_len;
    char restart_buf[sizeof("start_rpc_session")];
    size_t restart_len;
    uint8_t rx_buf[256];
} UartRpcSrv;

static UartRpcSrv* s_uart_bridge = NULL;

#define QFLIPPER_CLI_PROMPT "\r\n\r\n>: "
#define QFLIPPER_RPC_COMMAND "start_rpc_session"

/* UART0 is both the CYD data link and the default ESP32 console. The normal
 * ESP/Furi log paths are filtered in app_main, but esp_rom_printf() bypasses
 * those filters and several menu/loader diagnostics use it directly. Disable
 * the ROM printf channel while protobuf is active or a single debug line will
 * be inserted into the binary stream and qFlipper will wait forever. */
static void uart_rpc_set_binary_mode(UartRpcSrv* srv, bool enabled) {
    if(srv->rpc_mode == enabled) return;

    if(enabled) {
        /* Suppress the filtered log paths first, drain text already in the
         * hardware FIFO, then disconnect both ROM printf channels. RPC output
         * uses uart_write_bytes() and is unaffected by this. */
        srv->rpc_mode = true;
        esp_rom_output_tx_wait_idle(UART_NUM_0);
        esp_rom_install_channel_putc(1, NULL);
        esp_rom_install_channel_putc(2, NULL);
    } else {
        esp_rom_install_uart_printf();
        srv->rpc_mode = false;
    }
}

static void uart_rpc_write(const uint8_t* bytes, size_t len) {
    while(len) {
        const int written = uart_write_bytes(UART_NUM_0, (const char*)bytes, len);
        if(written <= 0) return;
        bytes += written;
        len -= (size_t)written;
    }
}

static void uart_rpc_send_prompt(void) {
    uart_rpc_write((const uint8_t*)QFLIPPER_CLI_PROMPT, strlen(QFLIPPER_CLI_PROMPT));
}

static void uart_rpc_send_bytes(void* ctx, uint8_t* bytes, size_t len) {
    UNUSED(ctx);
    uart_rpc_write(bytes, len);
}

/* qFlipper first opens a short-lived RPC session to identify the device, then
 * sends stop_session and opens a fresh one for normal UI work. UART/CH340 has
 * no disconnect signal, so explicitly return to the tiny CLI handshake after
 * that stop request; otherwise the second session waits forever for its
 * prompt. */
static void uart_rpc_session_closed(void* ctx) {
    UartRpcSrv* srv = ctx;
    /* This callback runs in the RPC worker and must not call RPC APIs. Ask
     * the UART transport thread to destroy the session and publish a prompt.
     * Keep binary mode enabled until the final response has left UART0. */
    srv->close_requested = true;
}

static void uart_rpc_session_terminated(void* ctx) {
    UartRpcSrv* srv = ctx;
    /* Session storage is released immediately after this callback returns. */
    srv->session_terminated = true;
}

static void uart_rpc_open_session(UartRpcSrv* srv) {
    if(srv->session) return;

    /* Protect one contiguous block before qFlipper can request screen
     * streaming. The WiFi app releases this reserve immediately before driver
     * init, so other apps do not pay for a running WiFi stack. */
    uart_rpc_set_binary_mode(srv, true);
    wlan_hal_reserve_memory();
    /* Must happen before rpc_session_open and Gui StartScreenStream fragment
     * the remaining internal heap. wlan_app() consumes this block later. */
    (void)wlan_app_reserve_memory();

    srv->session = rpc_session_open(srv->rpc, RpcOwnerUart);
    if(!srv->session) {
        uart_rpc_set_binary_mode(srv, false);
        return;
    }
    rpc_session_set_context(srv->session, srv);
    rpc_session_set_send_bytes_callback(srv->session, uart_rpc_send_bytes);
    rpc_session_set_close_callback(srv->session, uart_rpc_session_closed);
    rpc_session_set_terminated_callback(srv->session, uart_rpc_session_terminated);
}

static void uart_rpc_begin_close_session(UartRpcSrv* srv, bool reopen) {
    srv->close_requested = false;
    srv->reopen_requested = reopen;
    srv->cli_len = 0;
    srv->restart_len = 0;
    if(srv->session) {
        /* rpc_session_close() is asynchronous. Do not reuse or overwrite the
         * pointer until uart_rpc_session_terminated() confirms destruction. */
        rpc_session_close(srv->session);
    } else {
        srv->session_terminated = true;
    }
}

static void uart_rpc_finish_session_transition(UartRpcSrv* srv) {
    srv->session = NULL;
    srv->session_terminated = false;
    srv->cli_len = 0;
    srv->restart_len = 0;

    if(srv->reopen_requested) {
        srv->reopen_requested = false;
        uart_rpc_open_session(srv);
        if(srv->session) {
            uart_rpc_write(
                (const uint8_t*)(QFLIPPER_RPC_COMMAND "\r\n"),
                strlen(QFLIPPER_RPC_COMMAND "\r\n"));
        } else {
            uart_rpc_set_binary_mode(srv, false);
            uart_rpc_send_prompt();
        }
    } else {
        uart_rpc_set_binary_mode(srv, false);
        uart_rpc_send_prompt();
    }
}

static void uart_rpc_feed(UartRpcSrv* srv, const uint8_t* bytes, size_t len) {
    if(!srv->session) return;
    while(len) {
        const size_t available = rpc_session_get_available_size(srv->session);
        if(!available) {
            furi_delay_ms(1);
            continue;
        }
        const size_t chunk = available < len ? available : len;
        const size_t fed = rpc_session_feed(srv->session, bytes, chunk, FuriWaitForever);
        if(!fed) return;
        bytes += fed;
        len -= fed;
    }
}

static void uart_rpc_cli_rx(UartRpcSrv* srv, const uint8_t* bytes, size_t len) {
    for(size_t i = 0; i < len; ++i) {
        const char c = (char)bytes[i];
        if(c == '\r' || c == '\n') {
            srv->cli_buf[srv->cli_len] = '\0';
            if(strstr(srv->cli_buf, QFLIPPER_RPC_COMMAND)) {
                srv->cli_len = 0;
                uart_rpc_open_session(srv);
                if(srv->session) {
                    uart_rpc_write(
                        (const uint8_t*)(QFLIPPER_RPC_COMMAND "\r\n"),
                        strlen(QFLIPPER_RPC_COMMAND "\r\n"));
                    if(i + 1 < len) uart_rpc_feed(srv, bytes + i + 1, len - i - 1);
                }
                return;
            }
            srv->cli_len = 0;
            uart_rpc_send_prompt();
        } else if(srv->cli_len < sizeof(srv->cli_buf) - 1) {
            srv->cli_buf[srv->cli_len++] = c;
        }
    }
}

/* qFlipper opens the CH340 more than once: first to identify the device, then
 * again for normal use. A USB CDC device reports disconnects; CH340 does not.
 * Recognise its textual start command even after RPC is already active and
 * leave the existing session alive for the new serial handle. */
static void uart_rpc_binary_rx(UartRpcSrv* srv, const uint8_t* bytes, size_t len) {
    static const char command[] = QFLIPPER_RPC_COMMAND;
    for(size_t i = 0; i < len; ++i) {
        const uint8_t byte = bytes[i];
        if(srv->restart_len && byte == '\r') {
            if(srv->restart_len == sizeof(command) - 1) {
                srv->restart_len = 0;
                /* CH340 does not expose host port close to the ESP32. A
                 * repeated start command is the reconnect signal. */
                uart_rpc_begin_close_session(srv, true);
                continue;
            }
        }
        if(byte == (uint8_t)command[srv->restart_len]) {
            srv->restart_buf[srv->restart_len++] = (char)byte;
            if(srv->restart_len < sizeof(command) - 1) continue;
            continue; /* wait for the command's CR */
        }
        if(srv->restart_len) {
            uart_rpc_feed(srv, (const uint8_t*)srv->restart_buf, srv->restart_len);
            srv->restart_len = 0;
            if(byte == (uint8_t)command[0]) {
                srv->restart_buf[srv->restart_len++] = (char)byte;
                continue;
            }
        }
        uart_rpc_feed(srv, &byte, 1);
    }
}

static int32_t uart_rpc_thread(void* ctx) {
    UartRpcSrv* srv = ctx;
    uint32_t idle_ticks = 0;
    while(!srv->exiting) {
        if(srv->session_terminated) {
            uart_rpc_finish_session_transition(srv);
            idle_ticks = 0;
        } else if(srv->close_requested) {
            uart_rpc_begin_close_session(srv, false);
            idle_ticks = 0;
        }

        const int got = uart_read_bytes(UART_NUM_0, srv->rx_buf, sizeof(srv->rx_buf), pdMS_TO_TICKS(50));
        if(got > 0) {
            idle_ticks = 0;
            if(srv->rpc_mode) {
                uart_rpc_binary_rx(srv, srv->rx_buf, (size_t)got);
            } else if(!srv->session) {
                uart_rpc_cli_rx(srv, srv->rx_buf, (size_t)got);
            }
        } else if(!srv->rpc_mode && ++idle_ticks >= 10) {
            /* No DTR input is exposed by the CH340 to the ESP32. Repeating the
             * harmless CLI prompt lets a newly opened qFlipper port sync. */
            uart_rpc_send_prompt();
            idle_ticks = 0;
        }
    }
    return 0;
}

static bool uart_rpc_start(void) {
    if(s_uart_bridge) return true;
    if(uart_driver_install(UART_NUM_0, 4096, 4096, 0, NULL, 0) != ESP_OK) return false;
    UartRpcSrv* srv = calloc(1, sizeof(UartRpcSrv));
    if(!srv) {
        uart_driver_delete(UART_NUM_0);
        return false;
    }
    srv->rpc = furi_record_open(RECORD_RPC);
    srv->thread = furi_thread_try_alloc_ex("QflipperUart", 4096, uart_rpc_thread, srv);
    if(!srv->thread) {
        furi_record_close(RECORD_RPC);
        free(srv);
        uart_driver_delete(UART_NUM_0);
        return false;
    }
    s_uart_bridge = srv;
    furi_thread_start(srv->thread);
    return true;
}

static void uart_rpc_stop(void) {
    UartRpcSrv* srv = s_uart_bridge;
    if(!srv) return;
    s_uart_bridge = NULL;
    srv->exiting = true;
    furi_thread_join(srv->thread);
    furi_thread_free(srv->thread);
    if(srv->session) {
        /* The session terminates asynchronously. Detach the only callback
         * that still references srv before releasing the bridge context. */
        rpc_session_set_terminated_callback(srv->session, NULL);
        rpc_session_close(srv->session);
    }
    uart_rpc_set_binary_mode(srv, false);
    furi_record_close(RECORD_RPC);
    uart_driver_delete(UART_NUM_0);
    free(srv);
}

#endif /* QFLIPPER_HAVE_UART */

#if QFLIPPER_HAVE_COMPOSITE

/* ─────────────────────────────────────────────────────────────────────
 * CDC callbacks (from furi_hal_usb_cdc).
 *
 * These run on the TinyUSB task. We hand events to the bridge thread
 * via the message queue and return quickly.
 * ───────────────────────────────────────────────────────────────────── */

static void usb_rpc_post_event(UsbRpcSrv* srv, UsbRpcEvent ev) {
    furi_message_queue_put(srv->event_q, &ev, 0);
}

static void usb_rpc_cdc_tx_done(void* ctx) {
    usb_rpc_post_event(ctx, UsbRpcEventTxComplete);
}

static void usb_rpc_cdc_rx(void* ctx) {
    usb_rpc_post_event(ctx, UsbRpcEventRxAvailable);
}

static void usb_rpc_cdc_state(void* ctx, CdcState state) {
    UsbRpcSrv* srv = ctx;
    if(state == CdcStateDisconnected) {
        usb_rpc_post_event(srv, UsbRpcEventDisconnected);
    }
    /* Connected via DTR edge in ctrl_line callback below */
}

static void usb_rpc_cdc_ctrl_line(void* ctx, CdcCtrlLine ctrl) {
    UsbRpcSrv* srv = ctx;
    if(ctrl & CdcCtrlLineDTR) {
        usb_rpc_post_event(srv, UsbRpcEventConnected);
    } else {
        usb_rpc_post_event(srv, UsbRpcEventDisconnected);
    }
}

static CdcCallbacks usb_rpc_cdc_callbacks = {
    .tx_ep_callback = usb_rpc_cdc_tx_done,
    .rx_ep_callback = usb_rpc_cdc_rx,
    .state_callback = usb_rpc_cdc_state,
    .ctrl_line_callback = usb_rpc_cdc_ctrl_line,
    .config_callback = NULL,
};

/* ─────────────────────────────────────────────────────────────────────
 * RPC -> CDC (outbound)
 * ───────────────────────────────────────────────────────────────────── */

static void usb_rpc_send_bytes(void* ctx, uint8_t* bytes, size_t len) {
    (void)ctx;
    while(len > 0) {
        uint16_t chunk = (len > 0xFFFF) ? 0xFFFF : (uint16_t)len;
        furi_hal_cdc_send(USB_RPC_CDC_ITF, bytes, chunk);
        bytes += chunk;
        len -= chunk;
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * Session lifecycle
 * ───────────────────────────────────────────────────────────────────── */

static void usb_rpc_session_open(UsbRpcSrv* srv) {
    if(srv->session_open) return;

    srv->session = rpc_session_open(srv->rpc, RpcOwnerUsb);
    if(!srv->session) {
        FURI_LOG_E(TAG, "rpc_session_open failed");
        return;
    }
    rpc_session_set_context(srv->session, srv);
    rpc_session_set_send_bytes_callback(srv->session, usb_rpc_send_bytes);
    srv->session_open = true;
    FURI_LOG_I(TAG, "RPC session opened");
}

static void usb_rpc_session_close(UsbRpcSrv* srv) {
    if(!srv->session_open) return;

    rpc_session_close(srv->session);
    srv->session = NULL;
    srv->session_open = false;
    FURI_LOG_I(TAG, "RPC session closed");
}

static void usb_rpc_drain_rx(UsbRpcSrv* srv) {
    if(!srv->session_open) {
        uint8_t scratch[64];
        while(furi_hal_cdc_receive(USB_RPC_CDC_ITF, scratch, sizeof(scratch)) > 0) {
        }
        return;
    }

    /* Never read from the CDC FIFO unless we have downstream room for the whole
     * chunk we're about to consume. If the RPC stream is saturated we sit-and-
     * wait here so the TinyUSB RX FIFO fills up and USB-NAKs the host — the only
     * flow-control mechanism over USB-CDC. */
    while(true) {
        if(srv->exiting) return;
        size_t avail = rpc_session_get_available_size(srv->session);
        if(avail == 0) {
            furi_delay_ms(1);
            if(!srv->session_open || srv->exiting) return;
            continue;
        }

        size_t to_read = avail < sizeof(srv->rx_buf) ? avail : sizeof(srv->rx_buf);
        int32_t got = furi_hal_cdc_receive(USB_RPC_CDC_ITF, srv->rx_buf, to_read);
        if(got <= 0) {
            break;
        }

        size_t fed = rpc_session_feed(srv->session, srv->rx_buf, (size_t)got, FuriWaitForever);
        if(fed != (size_t)got) {
            FURI_LOG_E(TAG, "rpc_session_feed underran: %zu/%ld", fed, got);
            break;
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * CLI handshake — qFlipper drives the text CLI before piping protobuf.
 * We emulate just enough to satisfy the handshake, then flip into RPC mode.
 * ───────────────────────────────────────────────────────────────────── */

#define QFLIPPER_CLI_PROMPT  "\r\n\r\n>: "
#define QFLIPPER_RPC_COMMAND "start_rpc_session"

static void qflipper_cli_send_prompt(void) {
    furi_hal_cdc_send(
        USB_RPC_CDC_ITF, (uint8_t*)QFLIPPER_CLI_PROMPT, strlen(QFLIPPER_CLI_PROMPT));
}

static void qflipper_cli_rx(UsbRpcSrv* srv) {
    uint8_t buf[64];
    int32_t got;
    while((got = furi_hal_cdc_receive(USB_RPC_CDC_ITF, buf, sizeof(buf))) > 0) {
        for(int32_t i = 0; i < got; i++) {
            char c = (char)buf[i];

            if(c == '\r' || c == '\n') {
                srv->cli_buf[srv->cli_len] = '\0';

                if(strstr(srv->cli_buf, QFLIPPER_RPC_COMMAND) != NULL) {
                    furi_hal_cdc_send(
                        USB_RPC_CDC_ITF,
                        (uint8_t*)(QFLIPPER_RPC_COMMAND "\r\n"),
                        strlen(QFLIPPER_RPC_COMMAND "\r\n"));

                    srv->cli_len = 0;
                    srv->rpc_mode = true;
                    usb_rpc_session_open(srv);

                    if(srv->session_open && (i + 1) < got) {
                        rpc_session_feed(
                            srv->session, &buf[i + 1], (size_t)(got - i - 1), FuriWaitForever);
                    }
                    return;
                }

                srv->cli_len = 0;
                qflipper_cli_send_prompt();
            } else if(srv->cli_len < sizeof(srv->cli_buf) - 1) {
                srv->cli_buf[srv->cli_len++] = c;
            }
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * Bridge thread
 * ───────────────────────────────────────────────────────────────────── */

static void qflipper_bridge_run(UsbRpcSrv* srv) {
    UsbRpcEvent ev;
    while(true) {
        if(furi_message_queue_get(srv->event_q, &ev, FuriWaitForever) != FuriStatusOk) {
            continue;
        }

        switch(ev) {
        case UsbRpcEventExit:
            return;

        case UsbRpcEventConnected:
            if(srv->connected) break;
            srv->connected = true;
            srv->rpc_mode = false;
            srv->cli_len = 0;
            FURI_LOG_I(TAG, "DTR up — entering CLI, sending prompt");
            qflipper_cli_send_prompt();
            break;

        case UsbRpcEventDisconnected:
            if(!srv->connected) break;
            srv->connected = false;
            srv->rpc_mode = false;
            srv->cli_len = 0;
            FURI_LOG_I(TAG, "DTR down — closing RPC session");
            usb_rpc_session_close(srv);
            break;

        case UsbRpcEventRxAvailable:
            if(srv->rpc_mode) {
                usb_rpc_drain_rx(srv);
            } else {
                qflipper_cli_rx(srv);
            }
            break;

        case UsbRpcEventTxComplete:
            break;
        }
    }
}

/* The thread only runs the event loop. The composite is installed before the
 * thread starts (start()) and torn down after it joins (stop()), both on the
 * caller's (desktop) thread. The exiting flag lets the loop bail out of a
 * blocking RX drain so join() always completes promptly. */
static int32_t qflipper_bridge_thread(void* ctx) {
    UsbRpcSrv* srv = ctx;
    qflipper_bridge_run(srv);
    return 0;
}

#endif /* QFLIPPER_HAVE_COMPOSITE */

/* ─────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────── */

bool qflipper_bridge_start(void) {
#if QFLIPPER_HAVE_UART
    return uart_rpc_start();
#elif !QFLIPPER_HAVE_COMPOSITE
    return false;
#else
    if(s_bridge) return true; /* already active */

    UsbRpcSrv* srv = calloc(1, sizeof(UsbRpcSrv));
    if(!srv) return false;
    srv->event_q = furi_message_queue_alloc(16, sizeof(UsbRpcEvent));
    srv->session = NULL;
    srv->connected = false;
    srv->session_open = false;
    srv->rpc_mode = false;
    srv->exiting = false;
    srv->cli_len = 0;

    srv->rpc = furi_record_open(RECORD_RPC);

    /* Install the composite as a real Flipper Zero (0483:5740). Idempotent if
     * another consumer (USB-Storage) already installed it. */
    if(!furi_hal_usb_composite_install(0, 0, NULL, NULL)) {
        FURI_LOG_E(TAG, "composite_install failed");
        furi_record_close(RECORD_RPC);
        furi_message_queue_free(srv->event_q);
        free(srv);
        return false;
    }

    /* set_callbacks fires state/ctrl_line immediately with the current line
     * state, so if DTR is already high this seeds a Connected event into the
     * queue (drained once the thread starts). */
    furi_hal_cdc_set_callbacks(USB_RPC_CDC_ITF, &usb_rpc_cdc_callbacks, srv);

    srv->thread =
        furi_thread_try_alloc_ex("QflipperBridge", 8192, qflipper_bridge_thread, srv);
    if(!srv->thread) {
        FURI_LOG_E(TAG, "Not enough RAM for qFlipper bridge thread");
        furi_hal_cdc_set_callbacks(USB_RPC_CDC_ITF, NULL, NULL);
        furi_record_close(RECORD_RPC);
        furi_message_queue_free(srv->event_q);
        free(srv);
        return false;
    }
    s_bridge = srv;
    furi_thread_start(srv->thread);

    return true;
#endif
}

void qflipper_bridge_stop(void) {
#if QFLIPPER_HAVE_UART
    uart_rpc_stop();
#elif QFLIPPER_HAVE_COMPOSITE
    UsbRpcSrv* srv = s_bridge;
    if(!srv) return;
    s_bridge = NULL;

    /* Wake the loop out of any blocking RX drain, then ask it to exit. */
    srv->exiting = true;
    UsbRpcEvent ev = UsbRpcEventExit;
    furi_message_queue_put(srv->event_q, &ev, FuriWaitForever);

    furi_thread_join(srv->thread);
    furi_thread_free(srv->thread);

    /* Detach the RPC bridge but LEAVE the composite installed — exactly like
     * USB-Storage, which only toggles its MSC function. The esp_tinyusb build
     * here can't cleanly reinstall after furi_hal_usb_composite_uninstall()
     * (tusb_teardown is a no-op), so uninstalling here would break the next
     * Enable. The composite stays up until reboot; re-enabling just reattaches
     * the CDC callbacks. */
    furi_hal_cdc_set_callbacks(USB_RPC_CDC_ITF, NULL, NULL);
    usb_rpc_session_close(srv);
    furi_record_close(RECORD_RPC);

    furi_message_queue_free(srv->event_q);
    free(srv);
#endif
}

bool qflipper_bridge_is_active(void) {
#if QFLIPPER_HAVE_UART
    return s_uart_bridge != NULL;
#else
    return s_bridge != NULL;
#endif
}

bool qflipper_bridge_is_rpc_active(void) {
#if QFLIPPER_HAVE_UART
    return s_uart_bridge && s_uart_bridge->rpc_mode;
#else
    return s_bridge && s_bridge->rpc_mode;
#endif
}

bool qflipper_bridge_memory_handoff_begin(void) {
    if(!qflipper_bridge_is_rpc_active()) return false;

    rpc_system_gui_suspend_screen_stream();
    wlan_hal_stop();
    wlan_hal_release_memory_reserve();
    wlan_app_release_reserved_memory();
    return true;
}

void qflipper_bridge_memory_handoff_resume(void) {
    if(qflipper_bridge_is_rpc_active()) rpc_system_gui_resume_screen_stream();
}

void qflipper_bridge_memory_handoff_restore(void) {
    if(!qflipper_bridge_is_rpc_active()) return;

    rpc_system_gui_suspend_screen_stream();
    wlan_hal_stop();
    wlan_hal_release_memory_reserve();
    wlan_app_release_reserved_memory();
    wlan_hal_reserve_memory();
    (void)wlan_app_reserve_memory();
    rpc_system_gui_resume_screen_stream();
}
