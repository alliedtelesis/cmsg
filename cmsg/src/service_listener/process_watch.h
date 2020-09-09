/**
 * process_watch.h
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __PROCESS_WATCH_H_
#define __PROCESS_WATCH_H_

void process_watch_init (void);
void process_watch_deinit (void);
void process_watch_add (int pid);
void process_watch_remove (int pid);

#endif /* __PROCESS_WATCH_H_ */
