/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * process_watch.h
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __PROCESS_WATCH_H_
#define __PROCESS_WATCH_H_

#ifdef HAVE_PIDFD
void process_watch_init (void);
void process_watch_deinit (void);
void process_watch_add (pid_t pid);
void process_watch_remove (pid_t pid);
#else
static inline void
process_watch_init (void)
{
    return;
}

static inline void
process_watch_deinit (void)
{
    return;
}

static inline void
process_watch_add (pid_t pid)
{
    return;
}

static inline void
process_watch_remove (pid_t pid)
{
    return;
}
#endif /* HAVE_PIDFD */

#endif /* __PROCESS_WATCH_H_ */
