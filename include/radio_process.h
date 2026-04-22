#ifndef RADIO_PROCESS_H
#define RADIO_PROCESS_H

#include <sys/types.h>

#include "radio_config.h"

typedef struct {
    pid_t pid;
    int fd_in;
    int fd_out;
} ChildProc;

int child_is_running(ChildProc *p);
void child_kill_group(ChildProc *p);
int spawn_aplay(ChildProc *out, const RadioConfig *cfg);
int spawn_rtl_fm(ChildProc *out, const RadioConfig *cfg);

#endif
