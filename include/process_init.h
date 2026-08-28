
#ifndef PROCESS_INIT_H
#define PROCESS_INIT_H

#include <mqueue.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdint.h>

extern volatile sig_atomic_t exitRQ = 0;

int daemon_init(void);
int signals_init(void);

#endif
