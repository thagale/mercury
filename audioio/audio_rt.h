/* Real-time scheduling for the audio I/O threads.
 *
 * Copyright (C) 2026 Rhizomatica
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The threads that move samples between the device and the modem's rings
 * have deadlines of milliseconds: the ALSA capture and playback buffers are
 * 30 ms, and the Hermes-Lite 2 wants an EP2 packet every 2.625 ms.  The rest
 * of the process -- the LDPC decoders, the TX modulator, ARQ, the GUI bridge,
 * the TNC sockets -- is throughput work that the multi-second rings already
 * decouple from the device.  Under the default scheduler a busy host (the
 * four RX decoder workers on a Pi 4 are enough) can keep an audio thread off
 * the CPU for longer than its buffer, and that is a capture overrun (lost RX
 * samples, a frame that will not decode) or a playback underrun (a gap keyed
 * onto the air).  audio_rt_enter() moves the calling thread to SCHED_FIFO so
 * that it preempts all of that work.
 *
 * Use it ONLY in a thread that blocks every iteration -- in the device, in
 * poll(), or in a sleep.  A SCHED_FIFO thread that spins starves every
 * normal thread on its core.  Never use it for the modem/decoder threads:
 * they are CPU-bound by design.
 *
 * The default priority, 20, sits below the threaded interrupt handlers (50)
 * on a PREEMPT_RT kernel, so this never delays the USB/sound IRQ that brings
 * our own samples in, and below a sound server's own real-time thread.
 * MERCURY_RT_PRIORITY overrides it; MERCURY_RT_PRIORITY=0 turns this off.
 *
 * An unprivileged process needs an rtprio limit of at least the priority
 * (limits.conf / the "realtime" or "audio" group, or LimitRTPRIO= in a
 * systemd unit) or CAP_SYS_NICE.  Without one, the thread simply keeps its
 * normal priority, and the first refusal is logged with how to grant it.
 *
 * Linux (not Android) and FreeBSD only.  Windows (MMCSS) and macOS
 * (THREAD_TIME_CONSTRAINT_POLICY) have their own mechanisms, and Android's
 * AAudio already runs its own callback at real-time priority.
 */

#ifndef AUDIO_RT_H
#define AUDIO_RT_H

#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__FreeBSD__)
#define AUDIO_RT_SUPPORTED 1
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "hermes_log.h"

#define AUDIO_RT_DEFAULT_PRIORITY 20

static inline void audio_rt_enter(const char *log_tag)
{
#if defined(AUDIO_RT_SUPPORTED)
    /* One refusal is worth reporting; one per thread start is noise. */
    static _Atomic bool refusal_logged = false;

    int prio = AUDIO_RT_DEFAULT_PRIORITY;
    const char *env = getenv("MERCURY_RT_PRIORITY");
    if (env && env[0] != '\0')
    {
        char *end = NULL;
        long v = strtol(env, &end, 10);
        if (*end != '\0' || v < 0)
            HLOGW(log_tag, "ignoring MERCURY_RT_PRIORITY=\"%s\" "
                  "(want 0 to disable, or 1-99); using %d", env, prio);
        else if (v == 0)
        {
            HLOGI(log_tag, "real-time scheduling off (MERCURY_RT_PRIORITY=0)");
            return;
        }
        else
            prio = v > 99 ? 99 : (int)v;
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
              "CAP_SYS_NICE; MERCURY_RT_PRIORITY=0 silences this",
              prio, strerror(rc), prio);
#else
    (void) log_tag;
#endif
}

#endif /* AUDIO_RT_H */
