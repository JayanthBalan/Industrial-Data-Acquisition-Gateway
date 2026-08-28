
#include "process_init.h"

volatile sig_atomic_t exitRQ = 0;
static void signalHandler(int);

int signals_init(void) {
    struct sigaction sa = {0};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) != 0) {
        syslog(LOG_ERR, "sigaction(SIGINT) Failed: %s", strerror(errno));
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        syslog(LOG_ERR, "sigaction(SIGTERM) Failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

int daemon_init(void) {
    pid_t pid = fork();
    if(pid < 0) {
        syslog(LOG_ERR, "fork() Failed: %s", strerror(errno));
        return -1;
    }
    if(pid > 0) {
        closelog();
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid() Failed: %s", strerror(errno));
        return -1;
    }

    umask(0);
    if (chdir("/") == -1)
    {
        syslog(LOG_ERR, "chdir() Failed: %s", strerror(errno));
        return -1;
    }

    int devnull = open("/dev/null", O_RDWR);
    if (devnull == -1) {
        syslog(LOG_ERR, "open() Failed: %s", strerror(errno));
        return -1;
    }

    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    if (devnull > STDERR_FILENO) {
        close(devnull);
    }

    return 0;
}

static void signalHandler(int signo) {
    if(signo == SIGINT || signo == SIGTERM) {
        exitRQ = 1;
    }
}
