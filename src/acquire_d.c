
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <syslog.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <mqueue.h>

#define PORT "9000"
#define BACKLOG 10
#define CLIENT_ACCEPT_FAILURE_LIMIT_MAX 16

#define MESSAGE_HEADER_SIZE 6
#define MESSAGE_DATA_SIZE 12
#define MESSAGE_SIZE (MESSAGE_HEADER_SIZE + MESSAGE_DATA_SIZE)

#define SYNC_BYTE 0xAAU
#define MESSAGE_QUEUE_NAME "/mq-acquire-process"
#define MESSAGE_QUEUE_PRIORITY 3

static volatile sig_atomic_t exitRQ = 0;
static mqd_t transfer_mq;

static int daemon_init(void);
static int socket_init(void);
static int signals_init(void);
static int ipc_init(void);

static void signalHandler(int);
static void* connectionHandler(void*);
static inline void forwardPacket(uint8_t*, ssize_t);
static ssize_t recv_all(int, void*, size_t);

int main() {
    if(daemon_init() == -1) {
        closelog();
        return -1;
    }
    if(signals_init() == -1) {
        closelog();
        return -1;
    }
    if(ipc_init() == -1) {
        closelog();
        return -1;
    }

    int server_fd;
    if(socket_init(&server_fd) == -1) {
        closelog();
        return -1;
    }

    // Acquire Connections
    if (listen(server_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "listen() failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    // Infinite Loop: Main Thread
    static unsigned int client_accept_failure_count = 0;
    while(exitRQ == 0) {
        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int *client_fd = malloc(sizeof(int));
        if(client_fd == NULL) {
            syslog(LOG_ERR, "malloc() Error: %s", strerror(errno));
            close(server_fd);
            closelog();
            return -1;
        }

        // Wait and Collect new connection
        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if(*client_fd == -1) {
            if(errno == EINTR) {
                if(exitRQ == 1) {
                    free(client_fd);
                    break;
                }
                free(client_fd);
                continue;
            }
            syslog(LOG_ERR, "accept() Failed (%u): %s", client_accept_failure_count, strerror(errno));
            if(++client_accept_failure_count >= CLIENT_ACCEPT_FAILURE_LIMIT_MAX) {
                syslog(LOG_CRIT, "Critical Process Error: Multiple Client accept() Failures: Process Terminating");
                free(client_fd);
                close(server_fd);
                closelog();
                return -1;
            }
            free(client_fd);
            continue;
        }
        syslog(LOG_INFO, "Client %d Connection Accepted", *client_fd);

        // Thread Designation for Connection
        pthread_t threadConnection;
        if(pthread_create(&threadConnection, NULL, &connectionHandler, client_fd) != 0) {
            close(*client_fd);
            free(client_fd);
            syslog(LOG_ERR, "pthread_create() Failure");
            close(server_fd);
            closelog();
            return -1;
        }
        pthread_detach(threadConnection);
    }

    mq_close(transfer_mq);
    close(server_fd);
    closelog();
    return 0;
}

static void *connectionHandler(void *arg)
{
    int client_fd = *(int*)arg;
    free(arg);

    uint8_t msg_buffer[MESSAGE_SIZE];

    while (!exitRQ) {
        ssize_t bytes_read = recv_all(client_fd, msg_buffer, MESSAGE_HEADER_SIZE);
        if (bytes_read == 0) {
            syslog(LOG_INFO, "Client %d Disconnected", client_fd);
            break;
        }
        if (bytes_read < 0) {
            syslog(LOG_ERR, "recv(message header) Failed: %s", strerror(errno));
            break;
        }

        if (msg_buffer[0] != SYNC_BYTE)
        {
            syslog(LOG_WARNING, "Invalid Synchronization Byte from Client %d", client_fd);
            continue;
        }

        uint16_t msg_data_len = (uint16_t)msg_buffer[4] | ((uint16_t)msg_buffer[5] << 8);
        if (msg_data_len > MESSAGE_DATA_SIZE)
        {
            syslog(LOG_WARNING, "Packet Length Exceeds Maximum Permissible Length = %hu", msg_data_len);
            continue;
        }

        bytes_read = recv_all(client_fd, &msg_buffer[MESSAGE_HEADER_SIZE], msg_data_len);
        if (bytes_read == 0) {
            syslog(LOG_INFO, "Client %d Disconnected", client_fd);
            break;
        }
        if (bytes_read < 0) {
            syslog(LOG_ERR, "recv(message data) Failed: %s", strerror(errno));
            break;
        }

        forwardPacket(msg_buffer, MESSAGE_HEADER_SIZE + msg_data_len);
    }

    close(client_fd);
    return NULL;
}

static ssize_t recv_all(int fd, void *buffer, size_t len) {
    size_t total = 0;

    while (total < len) {
        ssize_t bytes_read = recv(fd, (uint8_t *)buffer + total, len - total, 0);
        if (bytes_read == 0) {
            return 0;
        }
        if (bytes_read < 0) {
            if (errno == EINTR)
            {
                if (exitRQ)
                {
                    return -1;
                }
                continue;
            }
            return -1;
        }

        total += (size_t)bytes_read;
    }

    return (ssize_t)total;
}

static inline void forwardPacket(uint8_t *packet, ssize_t len) {
    if(mq_send(transfer_mq, packet, len, MESSAGE_QUEUE_PRIORITY) == -1) {
        syslog(LOG_ERR, "mq_send() Failed: %s", strerror(errno));
    }
}

static void signalHandler(int signo) {
    if(signo == SIGINT || signo == SIGTERM) {
        exitRQ = 1;
    }
}

static int ipc_init(void) {
    transfer_mq = mq_open(MESSAGE_QUEUE_NAME, O_WRONLY | O_CREAT, 0644, NULL);
    if(transfer_mq == ((mqd_t) - 1)) {
        return -1;
    }

    return 0;
}

static int signals_init(void) {
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

static int socket_init(int *server_fd) {
    struct addrinfo serverHints = {0};
    struct addrinfo *serverInfo = NULL;
    *server_fd = -1;

    // Server Address Configuration
    serverHints.ai_family = AF_INET6;
    serverHints.ai_socktype = SOCK_STREAM;
    serverHints.ai_flags = AI_PASSIVE;
    serverHints.ai_protocol = 0;


    // Local Address Info
    int status = getaddrinfo(NULL, PORT, &serverHints, &serverInfo);
    if (status != 0) {
        syslog(LOG_ERR, "getaddrinfo() Failed: %s", gai_strerror(status));
        closelog();
        return -1;
    }

    // Address Iteration
retrySocket_signalEINTR:
    *server_fd = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol);
    if (*server_fd == -1) {
        if(errno == EINTR) {
            if(exitRQ) {
                return -1;
            }
            goto retrySocket_signalEINTR;
        }
        freeaddrinfo(serverInfo);
        syslog(LOG_ERR, "socket() Failed: %s", strerror(errno));
        return -1;
    }
retrySetSockOpt_signalEINTR:
    int opt = 1;
    if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        if(errno == EINTR) {
            if(exitRQ) {
                return -1;
            }
            goto retrySetSockOpt_signalEINTR;
        }
        freeaddrinfo(serverInfo);
        syslog(LOG_ERR, "getaddrinfo() Failed: %s", strerror(errno));
        return -1;
    }
retryBind_signalEINTR:
    if (bind(*server_fd, serverInfo->ai_addr, serverInfo->ai_addrlen) != 0) {
        if(errno == EINTR) {
            if(exitRQ) {
                return -1;
            }
            goto retryBind_signalEINTR;
        }
        freeaddrinfo(serverInfo);
        syslog(LOG_ERR, "getaddrinfo() Failed: %s", strerror(errno));
        return -1;
    }

    freeaddrinfo(serverInfo);
    serverInfo = NULL;
    return 0;
}

static int daemon_init(void) {
    openlog(__FILE__, LOG_PID | LOG_CONS, LOG_DAEMON);

    pid_t pid = fork();
    if(pid < 0) {
        syslog(LOG_ERR, "fork() Failed: %s", strerror(errno));
        return -1;
    }
    if(pid > 0) {
        closelog();
        exit(EXIT_SUCCESS);
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
