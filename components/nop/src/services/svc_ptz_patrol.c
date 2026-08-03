/**
 * @file svc_ptz_patrol.c
 * @brief PTZ patrol (cruise) execution engine. See nop_sdk/nop_ptz_patrol.h.
 */
#include "nop_sdk/nop_ptz_patrol.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)

#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_ptz.h"
#include "base/nop_mem.h"
#include "nop_sdk/osal/osal.h"

#include <pthread.h>
#include <string.h>

#define NOP_PTZ_PATROL_STEP_FLOOR_MS 20   /* min hold between moves (0-dwell guard) */
#define NOP_PTZ_PATROL_TICK_MS       20   /* stop-responsiveness granularity */

struct nop_ptz_patrol {
    osal_mutex_t   *ctl;          /* serializes start/stop/destroy as whole ops */
    osal_mutex_t   *mutex;        /* protects spots/step_count/running (worker vs query) */
    pthread_t       thread;
    volatile int    running;      /* 1 while the worker should keep cycling */
    int             thread_valid; /* 1 while thread is joinable (ctl-guarded) */
    int             channel;
    nop_ptz_spot_t *spots;        /* copied on start */
    int             spot_count;
    int             step_count;   /* goto-preset moves issued this run */
};

nop_ptz_patrol_t *nop_ptz_patrol_create(void)
{
    nop_ptz_patrol_t *engine = (nop_ptz_patrol_t *)nop_calloc(1, sizeof(*engine));
    if (!engine)
        return NULL;
    engine->ctl   = osal_mutex_create();
    engine->mutex = osal_mutex_create();
    if (!engine->ctl || !engine->mutex) {
        if (engine->ctl) osal_mutex_destroy(engine->ctl);
        if (engine->mutex) osal_mutex_destroy(engine->mutex);
        nop_free(engine);
        return NULL;
    }
    return engine;
}

/* Hold for @p dwell_seconds (with a small floor), waking every tick to notice a
 * stop request promptly. @return 1 to continue, 0 if stop was requested. */
static int patrol_hold(nop_ptz_patrol_t *engine, int dwell_seconds)
{
    long remaining_ms = (long)dwell_seconds * 1000;
    if (remaining_ms < NOP_PTZ_PATROL_STEP_FLOOR_MS)
        remaining_ms = NOP_PTZ_PATROL_STEP_FLOOR_MS;
    while (remaining_ms > 0) {
        if (!engine->running)
            return 0;
        osal_sleep_ms(NOP_PTZ_PATROL_TICK_MS);
        remaining_ms -= NOP_PTZ_PATROL_TICK_MS;
    }
    return engine->running;
}

static void *patrol_worker(void *arg)
{
    nop_ptz_patrol_t   *engine = (nop_ptz_patrol_t *)arg;
    const hal_ptz_if   *ptz = (const hal_ptz_if *)hal_registry_get(HAL_PTZ);
    int                 index = 0;

    while (engine->running) {
        nop_ptz_spot_t spot;
        osal_mutex_lock(engine->mutex);
        if (engine->spot_count <= 0) {
            osal_mutex_unlock(engine->mutex);
            break;
        }
        if (index >= engine->spot_count)
            index = 0;
        spot = engine->spots[index++];
        engine->step_count++;
        osal_mutex_unlock(engine->mutex);

        if (ptz && ptz->goto_preset)
            ptz->goto_preset(ptz->ctx, engine->channel, spot.preset);

        if (!patrol_hold(engine, spot.dwell_seconds))
            break;
    }
    return NULL;
}

/* Stop the worker and free its spots. Caller MUST hold engine->ctl (so this is
 * atomic vs another start/stop). The join is safe under ctl because the worker
 * only ever takes engine->mutex, never ctl — no deadlock. */
static void patrol_stop_inner(nop_ptz_patrol_t *engine)
{
    engine->running = 0;                   /* signal the worker to exit */
    if (engine->thread_valid) {
        pthread_join(engine->thread, NULL);
        engine->thread_valid = 0;
    }
    osal_mutex_lock(engine->mutex);
    nop_free(engine->spots);
    engine->spots = NULL;
    engine->spot_count = 0;
    osal_mutex_unlock(engine->mutex);
}

nop_status_t nop_ptz_patrol_start(nop_ptz_patrol_t *engine, int channel,
                                  const nop_ptz_spot_t *spots, int count)
{
    const hal_ptz_if *ptz;
    nop_ptz_spot_t   *copy;

    if (!engine || !spots || count <= 0)
        return NOP_ERR_PARAM;
    ptz = (const hal_ptz_if *)hal_registry_get(HAL_PTZ);
    if (!ptz || !ptz->goto_preset)
        return NOP_ERR_NOTIMPL;

    copy = (nop_ptz_spot_t *)nop_calloc((size_t)count, sizeof(nop_ptz_spot_t));
    if (!copy)
        return NOP_ERR_STATE;
    memcpy(copy, spots, (size_t)count * sizeof(nop_ptz_spot_t));

    /* Hold ctl across the whole start so a concurrent stop/start cannot see a
     * half-set thread_valid and skip the join / free live spots. */
    osal_mutex_lock(engine->ctl);
    patrol_stop_inner(engine);             /* replace any running patrol */

    osal_mutex_lock(engine->mutex);
    engine->channel    = channel;
    engine->spots      = copy;
    engine->spot_count = count;
    engine->step_count = 0;
    engine->running    = 1;
    osal_mutex_unlock(engine->mutex);

    if (pthread_create(&engine->thread, NULL, patrol_worker, engine) != 0) {
        osal_mutex_lock(engine->mutex);
        engine->running = 0;
        nop_free(engine->spots);
        engine->spots = NULL;
        engine->spot_count = 0;
        osal_mutex_unlock(engine->mutex);
        osal_mutex_unlock(engine->ctl);
        return NOP_ERR_STATE;
    }
    engine->thread_valid = 1;              /* set under ctl, before releasing it */
    osal_mutex_unlock(engine->ctl);
    return NOP_OK;
}

nop_status_t nop_ptz_patrol_stop(nop_ptz_patrol_t *engine)
{
    if (!engine)
        return NOP_ERR_PARAM;
    osal_mutex_lock(engine->ctl);
    patrol_stop_inner(engine);
    osal_mutex_unlock(engine->ctl);
    return NOP_OK;
}

void nop_ptz_patrol_destroy(nop_ptz_patrol_t *engine)
{
    if (!engine)
        return;
    nop_ptz_patrol_stop(engine);
    osal_mutex_destroy(engine->ctl);
    osal_mutex_destroy(engine->mutex);
    nop_free(engine);
}

int nop_ptz_patrol_is_running(nop_ptz_patrol_t *engine)
{
    int running;
    if (!engine)
        return 0;
    osal_mutex_lock(engine->mutex);
    running = engine->running;
    osal_mutex_unlock(engine->mutex);
    return running;
}

int nop_ptz_patrol_step_count(nop_ptz_patrol_t *engine)
{
    int count;
    if (!engine)
        return 0;
    osal_mutex_lock(engine->mutex);
    count = engine->step_count;
    osal_mutex_unlock(engine->mutex);
    return count;
}

#else /* non-POSIX: no background patrol */

nop_ptz_patrol_t *nop_ptz_patrol_create(void) { return NULL; }
void nop_ptz_patrol_destroy(nop_ptz_patrol_t *e) { (void)e; }
nop_status_t nop_ptz_patrol_start(nop_ptz_patrol_t *e, int channel,
                                  const nop_ptz_spot_t *spots, int count)
{ (void)e; (void)channel; (void)spots; (void)count; return NOP_ERR_NOTIMPL; }
nop_status_t nop_ptz_patrol_stop(nop_ptz_patrol_t *e) { (void)e; return NOP_OK; }
int nop_ptz_patrol_is_running(nop_ptz_patrol_t *e) { (void)e; return 0; }
int nop_ptz_patrol_step_count(nop_ptz_patrol_t *e) { (void)e; return 0; }

#endif
