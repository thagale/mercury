/* Real-time scheduling for the audio I/O threads (see audio_rt.h).
 *
 * Copyright (C) 2026 Rhizomatica
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "audio_rt.h"

#include <stdatomic.h>
#include <stdbool.h>

#if defined(AUDIO_RT_SUPPORTED)
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "hermes_log.h"

static _Atomic int g_rt_priority = AUDIO_RT_DEFAULT_PRIORITY;

void audio_rt_set_priority(int prio)
{
    if (prio < 0)
        prio = 0;
    if (prio > 99)
        prio = 99;
    atomic_store(&g_rt_priority, prio);
}

void audio_rt_enter(const char *log_tag)
{
#if defined(AUDIO_RT_SUPPORTED)
    /* Each thread calls this, but a setting is worth reporting once per
     * process, not once per thread (or per thread start). */
    static _Atomic bool env_warned     = false;
    static _Atomic bool off_logged     = false;
    static _Atomic bool refusal_logged = false;

    int prio = atomic_load(&g_rt_priority);
    const char *source = "[audio] rt_priority";
    const char *env = getenv("MERCURY_RT_PRIORITY");
    if (env && env[0] != '\0')
    {
        char *end = NULL;
        long v = strtol(env, &end, 10);
        if (*end != '\0' || v < 0)
        {
            if (!atomic_exchange(&env_warned, true))
                HLOGW(log_tag, "ignoring MERCURY_RT_PRIORITY=\"%s\" "
                      "(want 0 to disable, or 1-99); using %d", env, prio);
        }
        else
        {
            prio = v > 99 ? 99 : (int)v;
            source = "MERCURY_RT_PRIORITY";
        }
    }

    if (prio == 0)
    {
        if (!atomic_exchange(&off_logged, true))
            HLOGI(log_tag, "real-time scheduling off (%s = 0)", source);
        return;
    }

    /* Linux is 1..99, FreeBSD 0..31: clamp rather than fail on EINVAL. */
    int lo = sched_get_priority_min(SCHED_FIFO);
    int hi = sched_get_priority_max(SCHED_FIFO);
    if (lo >= 0 && prio < lo)
        prio = lo;
    if (hi >= 0 && prio > hi)
        prio = hi;

    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = prio;
    int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (rc == 0)
    {
        HLOGI(log_tag, "real-time scheduling: SCHED_FIFO priority %d", prio);
        return;
    }

    if (!atomic_exchange(&refusal_logged, true))
        HLOGW(log_tag, "could not switch to SCHED_FIFO priority %d (%s); audio "
              "threads run at normal priority.  Grant an rtprio limit >= %d "
              "(limits.conf, or LimitRTPRIO= for a systemd service) or "
              "CAP_SYS_NICE; [audio] rt_priority = 0 silences this",
              prio, strerror(rc), prio);
#else
    (void) log_tag;
#endif
}
