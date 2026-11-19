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
 * [audio] rt_priority in mercury.ini sets it (audio_rt_set_priority()), and
 * MERCURY_RT_PRIORITY in the environment overrides that; 0 turns this off.
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
#endif

/* Keep in step with CFG_RT_PRIORITY_DEFAULT (common/cfg_utils.h). */
#define AUDIO_RT_DEFAULT_PRIORITY 20

/* The configured priority ([audio] rt_priority): 0 for off, 1-99.  Call it
 * before the audio threads start; out-of-range values are clamped. */
void audio_rt_set_priority(int prio);

/* Move the calling thread to SCHED_FIFO at the configured priority, or at
 * MERCURY_RT_PRIORITY if that is set.  Does nothing where unsupported. */
void audio_rt_enter(const char *log_tag);

#endif /* AUDIO_RT_H */
