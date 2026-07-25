#include "subghz_worker.h"

#include <furi.h>
#include <sdkconfig.h>

#define TAG "SubGhzWorker"

#if CONFIG_IDF_TARGET_ESP32 && !CONFIG_SPIRAM
/* The original queue stores 4096 radio edges (roughly 16 KiB). On classic
 * ESP32 this competes with the qFlipper stream and WiFi reserve. The worker
 * consumes edges continuously, so 1024 entries retain a useful burst margin
 * while returning about 12 KiB of internal RAM. */
#define SUBGHZ_WORKER_BUFFER_ITEMS 1024u
#else
#define SUBGHZ_WORKER_BUFFER_ITEMS 4096u
#endif

struct SubGhzWorker {
    FuriThread* thread;
    FuriStreamBuffer* stream;

    volatile bool running;
    volatile bool overrun;

    LevelDuration filter_level_duration;
    uint16_t filter_duration;

    SubGhzWorkerOverrunCallback overrun_callback;
    SubGhzWorkerPairCallback pair_callback;
    void* context;
};

/** Rx callback timer
 * 
 * @param level received signal level
 * @param duration received signal duration
 * @param context 
 */
void subghz_worker_rx_callback(bool level, uint32_t duration, void* context) {
    SubGhzWorker* instance = context;

    LevelDuration level_duration = level_duration_make(level, duration);
    if(instance->overrun) {
        instance->overrun = false;
        level_duration = level_duration_reset();
    }
    size_t ret =
        furi_stream_buffer_send(instance->stream, &level_duration, sizeof(LevelDuration), 0);
    if(sizeof(LevelDuration) != ret) instance->overrun = true;
}

/** Worker callback thread
 * 
 * @param context 
 * @return exit code 
 */
static int32_t subghz_worker_thread_callback(void* context) {
    SubGhzWorker* instance = context;

    LevelDuration level_duration;

    while(instance->running) {
        int ret = furi_stream_buffer_receive(
            instance->stream, &level_duration, sizeof(LevelDuration), 10);
        if(ret == sizeof(LevelDuration)) {
            if(level_duration_is_reset(level_duration)) {
                if(instance->overrun_callback) instance->overrun_callback(instance->context);
            } else {
                bool level = level_duration_get_level(level_duration);
                uint32_t duration = level_duration_get_duration(level_duration);

                if((duration < instance->filter_duration) ||
                   (instance->filter_level_duration.level == level)) {
                    instance->filter_level_duration.duration += duration;

                } else if(instance->filter_level_duration.level != level) {
                    if(instance->pair_callback)
                        instance->pair_callback(
                            instance->context,
                            instance->filter_level_duration.level,
                            instance->filter_level_duration.duration);

                    instance->filter_level_duration.duration = duration;
                    instance->filter_level_duration.level = level;
                }
            }
        }
    }

    return 0;
}

SubGhzWorker* subghz_worker_alloc(void) {
    SubGhzWorker* instance = calloc(1, sizeof(SubGhzWorker));
    if(!instance) return NULL;

    /* Allocate the edge queue first because it is the larger contiguous
     * block. Both allocations are recoverable at this app boundary. */
    instance->stream = furi_stream_buffer_try_alloc(
        sizeof(LevelDuration) * SUBGHZ_WORKER_BUFFER_ITEMS, sizeof(LevelDuration));
    instance->thread =
        furi_thread_try_alloc_ex("SubGhzWorker", 3072, subghz_worker_thread_callback, instance);
    if(!instance->stream || !instance->thread) {
        if(instance->thread) furi_thread_free(instance->thread);
        if(instance->stream) furi_stream_buffer_free(instance->stream);
        free(instance);
        FURI_LOG_E(TAG, "Not enough RAM for radio worker");
        return NULL;
    }

    //setting default filter in us
    instance->filter_duration = 30;

    return instance;
}

void subghz_worker_free(SubGhzWorker* instance) {
    furi_check(instance);

    furi_stream_buffer_free(instance->stream);
    furi_thread_free(instance->thread);

    free(instance);
}

void subghz_worker_set_overrun_callback(
    SubGhzWorker* instance,
    SubGhzWorkerOverrunCallback callback) {
    furi_check(instance);
    instance->overrun_callback = callback;
}

void subghz_worker_set_pair_callback(SubGhzWorker* instance, SubGhzWorkerPairCallback callback) {
    furi_check(instance);
    instance->pair_callback = callback;
}

void subghz_worker_set_context(SubGhzWorker* instance, void* context) {
    furi_check(instance);
    instance->context = context;
}

void subghz_worker_start(SubGhzWorker* instance) {
    furi_check(instance);
    furi_check(!instance->running);

    instance->running = true;

    furi_thread_start(instance->thread);
}

void subghz_worker_stop(SubGhzWorker* instance) {
    furi_check(instance);
    furi_check(instance->running);

    instance->running = false;

    furi_thread_join(instance->thread);
}

bool subghz_worker_is_running(SubGhzWorker* instance) {
    furi_check(instance);
    return instance->running;
}

void subghz_worker_set_filter(SubGhzWorker* instance, uint16_t timeout) {
    furi_check(instance);
    instance->filter_duration = timeout;
}
