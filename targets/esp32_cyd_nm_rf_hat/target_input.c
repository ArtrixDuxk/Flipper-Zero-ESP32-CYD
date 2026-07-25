/**
 * @file target_input.c
 * Input driver for ESP32-2432S028 (CYD) + NM-RF-HAT
 *
 * Resistive XPT2046 is noisy: we debounce press/release, ignore the first
 * samples after contact, and classify gestures from start → stable end point
 * (not a jittery last sample).
 *
 * Mapping (screen coordinates after touch transform):
 *   Swipe up / down     -> InputKeyUp / InputKeyDown
 *   Swipe left          -> InputKeyBack
 *   Swipe right         -> InputKeyRight
 *
 *   Tap left edge       -> Back
 *   Tap right edge      -> Ok
 *   Tap upper / lower   -> Up / Down
 *
 *   BOOT short click    -> Back
 *   BOOT long / double  -> Ok
 */

#include "target_input.h"

#include <furi_hal_touch.h>
#include <furi_hal_display.h>
#include <furi_hal_resources.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <stdlib.h>

#define TAG "InputTouchCyd"

#define INPUT_BUTTON_DEBOUNCE_POLLS    2U
#define INPUT_BUTTON_SHORT_PRESS_MAX_MS 450U
#define INPUT_BUTTON_DOUBLE_CLICK_MS   350U

/* Touch contact debounce (polls @ ~4 ms → ~12–20 ms) */
#define INPUT_PRESS_DEBOUNCE   3U
#define INPUT_RELEASE_DEBOUNCE 4U
/* Drop first samples after contact — resistive contact is unstable */
#define INPUT_SETTLE_SAMPLES   3U

/* Gesture thresholds in panel pixels (320×240 landscape) */
#define INPUT_SWIPE_MIN_DELTA  36
#define INPUT_TAP_MAX_DELTA    22
/* Dominant axis must beat the other by this ratio (×10 → 13 = 1.3×) */
#define INPUT_AXIS_RATIO_X10   12

#define INPUT_ZONE_EDGE_PCT    32
#define INPUT_COOLDOWN_NAV_MS  90
#define INPUT_COOLDOWN_OK_MS   220
/* Hold still this long → Back (escape hatch out of any app/scene) */
#define INPUT_LONG_BACK_MS     550

#define INPUT_TOUCH_DEBUG 0

typedef struct {
    bool raw_pressed;
    bool debounced_pressed;
    uint8_t debounce_polls;
    uint32_t press_started_at;
    bool pending_single_click;
    bool second_click_started;
    uint32_t first_click_released_at;
} InputBootButtonState;

typedef struct {
    bool active;
    uint8_t press_count;
    uint8_t release_count;
    uint8_t settle_left;
    uint16_t start_x;
    uint16_t start_y;
    uint16_t last_x;
    uint16_t last_y;
    /* Running average of recent points for stable end position */
    int32_t avg_x;
    int32_t avg_y;
    uint16_t avg_n;
    /* Peak excursion from start (detect real swipe even if finger retracts) */
    int16_t peak_dx;
    int16_t peak_dy;
    uint32_t press_started_at;
    bool long_back_sent;
} InputTouchGesture;

static InputBootButtonState boot_button;
static InputTouchGesture gesture;
static uint32_t last_emit_at;
static InputKey last_emit_key;

static void input_publish(FuriPubSub* pubsub, InputKey key, InputType type, uint32_t sequence) {
    InputEvent event = {
        .sequence_source = INPUT_SEQUENCE_SOURCE_TOUCH,
        .sequence_counter = sequence,
        .key = key,
        .type = type,
    };
    furi_pubsub_publish(pubsub, &event);
}

static uint32_t input_cooldown_for(InputKey key) {
    if(key == InputKeyOk) return INPUT_COOLDOWN_OK_MS;
    return INPUT_COOLDOWN_NAV_MS;
}

static void input_emit_short(FuriPubSub* pubsub, InputKey key, uint32_t* sequence_counter) {
    uint32_t now = furi_get_tick();
    uint32_t cd = furi_ms_to_ticks(input_cooldown_for(key));
    if(last_emit_at != 0 && (now - last_emit_at) < cd) {
        /* Allow rapid Up/Down if key changed; block same-key spam and Ok spam */
        if(key == last_emit_key || key == InputKeyOk) {
            return;
        }
        if((now - last_emit_at) < furi_ms_to_ticks(40)) {
            return;
        }
    }
    last_emit_at = now;
    last_emit_key = key;

    const uint32_t seq = ++(*sequence_counter);
    input_publish(pubsub, key, InputTypePress, seq);
    input_publish(pubsub, key, InputTypeShort, seq);
    input_publish(pubsub, key, InputTypeRelease, seq);
#if INPUT_TOUCH_DEBUG
    FURI_LOG_I(TAG, "emit key=%d", (int)key);
#endif
}

static int16_t input_iabs16(int16_t v) {
    return v < 0 ? (int16_t)-v : v;
}

static bool input_classify_gesture(
    int16_t dx,
    int16_t dy,
    int16_t peak_dx,
    int16_t peak_dy,
    InputKey* key) {
    /* Prefer peak excursion if larger — finger often retracts on lift */
    int16_t use_dx = dx;
    int16_t use_dy = dy;
    if(input_iabs16(peak_dx) > input_iabs16(dx)) use_dx = peak_dx;
    if(input_iabs16(peak_dy) > input_iabs16(dy)) use_dy = peak_dy;

    int16_t adx = input_iabs16(use_dx);
    int16_t ady = input_iabs16(use_dy);

    /* In the grey zone between tap and swipe: still treat as tap (never drop). */
    if(adx < INPUT_SWIPE_MIN_DELTA && ady < INPUT_SWIPE_MIN_DELTA) {
        return false; /* caller does tap */
    }

    /* Dominant axis with minimum ratio to avoid diagonal flip-flops */
    if(ady * 10 >= adx * INPUT_AXIS_RATIO_X10) {
        *key = (use_dy < 0) ? InputKeyUp : InputKeyDown;
        return true;
    }
    if(adx * 10 >= ady * INPUT_AXIS_RATIO_X10) {
        /* Left → Left (widget "Back" buttons + view_dispatcher touch→Back).
         * Right → Right. Exit apps also works via long-press Back. */
        *key = (use_dx < 0) ? InputKeyLeft : InputKeyRight;
        return true;
    }
    /* Nearly diagonal: prefer vertical for menu nav */
    *key = (use_dy < 0) ? InputKeyUp : InputKeyDown;
    return true;
}

static InputKey input_tap_zone_key(uint16_t x, uint16_t y) {
    const uint16_t w = furi_hal_display_get_h_res();
    const uint16_t h = furi_hal_display_get_v_res();
    if(w == 0 || h == 0) return InputKeyOk;

    const uint16_t edge_x = (uint16_t)((w * INPUT_ZONE_EDGE_PCT) / 100);
    const uint16_t edge_y = (uint16_t)((h * 35) / 100);

    /* Left edge = Left (Back via view_dispatcher for touch) / right = Ok */
    if(x < edge_x) return InputKeyLeft;
    if(x >= (w - edge_x)) return InputKeyOk;
    /* Center band: upper third Up, lower third Down, middle = Ok (confirm) */
    if(y < edge_y) return InputKeyUp;
    if(y >= (h - edge_y)) return InputKeyDown;
    return InputKeyOk;
}

static void input_gesture_reset(void) {
    gesture.active = false;
    gesture.press_count = 0;
    gesture.release_count = 0;
    gesture.settle_left = 0;
    gesture.avg_n = 0;
    gesture.avg_x = 0;
    gesture.avg_y = 0;
    gesture.peak_dx = 0;
    gesture.peak_dy = 0;
    gesture.press_started_at = 0;
    gesture.long_back_sent = false;
}

static void input_gesture_on_sample(uint16_t x, uint16_t y) {
    if(gesture.settle_left > 0) {
        gesture.settle_left--;
        /* Re-anchor start after settle so first junk samples don't define origin */
        if(gesture.settle_left == 0) {
            gesture.start_x = x;
            gesture.start_y = y;
            gesture.last_x = x;
            gesture.last_y = y;
            gesture.avg_x = x;
            gesture.avg_y = y;
            gesture.avg_n = 1;
            gesture.peak_dx = 0;
            gesture.peak_dy = 0;
        }
        return;
    }

    gesture.last_x = x;
    gesture.last_y = y;

    /* Exponential-ish running average of last points (stable end) */
    if(gesture.avg_n == 0) {
        gesture.avg_x = x;
        gesture.avg_y = y;
        gesture.avg_n = 1;
    } else {
        gesture.avg_x = (gesture.avg_x * 3 + x) / 4;
        gesture.avg_y = (gesture.avg_y * 3 + y) / 4;
        if(gesture.avg_n < 64) gesture.avg_n++;
    }

    int16_t dx = (int16_t)x - (int16_t)gesture.start_x;
    int16_t dy = (int16_t)y - (int16_t)gesture.start_y;
    if(input_iabs16(dx) > input_iabs16(gesture.peak_dx)) gesture.peak_dx = dx;
    if(input_iabs16(dy) > input_iabs16(gesture.peak_dy)) gesture.peak_dy = dy;
}

static void input_gesture_finish(FuriPubSub* pubsub, uint32_t* sequence_counter) {
    if(gesture.settle_left > 0 || gesture.avg_n == 0) {
        input_gesture_reset();
        return;
    }

    /* Long-hold already sent Back — ignore release */
    if(gesture.long_back_sent) {
        input_gesture_reset();
        return;
    }

    int16_t end_x = (int16_t)gesture.avg_x;
    int16_t end_y = (int16_t)gesture.avg_y;
    int16_t dx = end_x - (int16_t)gesture.start_x;
    int16_t dy = end_y - (int16_t)gesture.start_y;

    InputKey key;
    if(input_classify_gesture(dx, dy, gesture.peak_dx, gesture.peak_dy, &key)) {
        input_emit_short(pubsub, key, sequence_counter);
    } else {
        /* Tap: use start position (where user intended) */
        key = input_tap_zone_key(gesture.start_x, gesture.start_y);
        input_emit_short(pubsub, key, sequence_counter);
    }

#if INPUT_TOUCH_DEBUG
    FURI_LOG_I(
        TAG,
        "gesture dx=%d dy=%d peak=%d,%d -> key=%d start=%u,%u",
        (int)dx,
        (int)dy,
        (int)gesture.peak_dx,
        (int)gesture.peak_dy,
        (int)key,
        gesture.start_x,
        gesture.start_y);
#endif
    input_gesture_reset();
}

/* ---- BOOT button ---- */

static uint32_t input_elapsed_ticks(uint32_t started_at, uint32_t now) {
    return now - started_at;
}

static bool input_boot_button_is_pressed(void) {
    return gpio_get_level((gpio_num_t)gpio_button_boot.pin) == 0;
}

static void input_boot_button_init(void) {
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << gpio_button_boot.pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&config);
}

static void input_boot_button_init_state(InputBootButtonState* state) {
    state->raw_pressed = input_boot_button_is_pressed();
    state->debounced_pressed = state->raw_pressed;
    state->debounce_polls = INPUT_BUTTON_DEBOUNCE_POLLS;
    state->press_started_at = 0;
    state->pending_single_click = false;
    state->second_click_started = false;
    state->first_click_released_at = 0;
}

static void input_boot_button_reset_clicks(InputBootButtonState* state) {
    state->pending_single_click = false;
    state->second_click_started = false;
    state->first_click_released_at = 0;
}

static void input_boot_button_handle_press(InputBootButtonState* state, uint32_t now) {
    state->press_started_at = now;
    if(state->pending_single_click) {
        state->second_click_started = true;
    }
}

static void input_boot_button_handle_release(
    InputBootButtonState* state,
    FuriPubSub* pubsub,
    uint32_t now,
    uint32_t short_press_max_ticks,
    uint32_t double_click_ticks,
    uint32_t* sequence_counter) {
    bool is_short = input_elapsed_ticks(state->press_started_at, now) <= short_press_max_ticks;

    if(!is_short) {
        input_emit_short(pubsub, InputKeyOk, sequence_counter);
        input_boot_button_reset_clicks(state);
        return;
    }

    if(!state->pending_single_click) {
        state->pending_single_click = true;
        state->second_click_started = false;
        state->first_click_released_at = now;
        return;
    }

    if(input_elapsed_ticks(state->first_click_released_at, now) <= double_click_ticks) {
        input_emit_short(pubsub, InputKeyOk, sequence_counter);
        input_boot_button_reset_clicks(state);
        return;
    }

    input_emit_short(pubsub, InputKeyBack, sequence_counter);
    state->pending_single_click = true;
    state->second_click_started = false;
    state->first_click_released_at = now;
}

static void input_boot_button_poll(
    InputBootButtonState* state,
    FuriPubSub* pubsub,
    uint32_t now,
    uint32_t short_press_max_ticks,
    uint32_t double_click_ticks,
    uint32_t* sequence_counter) {
    bool raw = input_boot_button_is_pressed();

    if(raw == state->raw_pressed) {
        if(state->debounce_polls < INPUT_BUTTON_DEBOUNCE_POLLS) state->debounce_polls++;
    } else {
        state->raw_pressed = raw;
        state->debounce_polls = 1;
    }

    if(state->debounce_polls >= INPUT_BUTTON_DEBOUNCE_POLLS &&
       state->debounced_pressed != state->raw_pressed) {
        state->debounced_pressed = state->raw_pressed;
        if(state->debounced_pressed) {
            input_boot_button_handle_press(state, now);
        } else {
            input_boot_button_handle_release(
                state, pubsub, now, short_press_max_ticks, double_click_ticks, sequence_counter);
        }
    }

    if(state->pending_single_click && !state->second_click_started &&
       (input_elapsed_ticks(state->first_click_released_at, now) > double_click_ticks)) {
        input_emit_short(pubsub, InputKeyBack, sequence_counter);
        input_boot_button_reset_clicks(state);
    }
}

void target_input_init(void) {
    furi_hal_touch_init();
    input_boot_button_init();
    input_boot_button_init_state(&boot_button);
    input_gesture_reset();
    last_emit_at = 0;
    last_emit_key = InputKeyMAX;
    FURI_LOG_I(
        TAG,
        "CYD input v3: swipe U/D nav L=Left R=Right | tap L=Left R=Ok | hold=Back | BOOT short=Back long=Ok");
}

void target_input_poll(FuriPubSub* pubsub, uint32_t* sequence_counter) {
    uint32_t now = furi_get_tick();
    input_boot_button_poll(
        &boot_button,
        pubsub,
        now,
        furi_ms_to_ticks(INPUT_BUTTON_SHORT_PRESS_MAX_MS),
        furi_ms_to_ticks(INPUT_BUTTON_DOUBLE_CLICK_MS),
        sequence_counter);

    TouchData touch;
    if(!furi_hal_touch_read(&touch)) return;

    const bool raw_touching = (touch.finger_count > 0);

    if(raw_touching) {
        gesture.release_count = 0;
        if(!gesture.active) {
            if(gesture.press_count < 255) gesture.press_count++;
            if(gesture.press_count >= INPUT_PRESS_DEBOUNCE) {
                gesture.active = true;
                gesture.settle_left = INPUT_SETTLE_SAMPLES;
                gesture.start_x = touch.x;
                gesture.start_y = touch.y;
                gesture.last_x = touch.x;
                gesture.last_y = touch.y;
                gesture.avg_x = touch.x;
                gesture.avg_y = touch.y;
                gesture.avg_n = 0;
                gesture.peak_dx = 0;
                gesture.peak_dy = 0;
                gesture.press_started_at = now;
                gesture.long_back_sent = false;
            }
        } else {
            input_gesture_on_sample(touch.x, touch.y);
            /* Long stationary hold → Back (escape from NFC / any app) */
            if(!gesture.long_back_sent && gesture.settle_left == 0 &&
               gesture.press_started_at != 0 &&
               (now - gesture.press_started_at) >= furi_ms_to_ticks(INPUT_LONG_BACK_MS)) {
                int16_t adx = input_iabs16(gesture.peak_dx);
                int16_t ady = input_iabs16(gesture.peak_dy);
                if(adx <= INPUT_TAP_MAX_DELTA && ady <= INPUT_TAP_MAX_DELTA) {
                    input_emit_short(pubsub, InputKeyBack, sequence_counter);
                    gesture.long_back_sent = true;
                }
            }
        }
        return;
    }

    /* Not touching */
    gesture.press_count = 0;
    if(!gesture.active) return;

    if(gesture.release_count < 255) gesture.release_count++;
    if(gesture.release_count < INPUT_RELEASE_DEBOUNCE) {
        return; /* wait for stable lift */
    }

    input_gesture_finish(pubsub, sequence_counter);
}
