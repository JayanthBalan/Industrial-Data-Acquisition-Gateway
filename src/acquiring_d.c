#include "process_init.h"
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define PORT "9000"
#define BACKLOG 10
#define CLIENT_ACCEPT_FAILURE_LIMIT_MAX 16

#define PACKET_HEADER_FIELDS_SIZE 6
#define PACKET_DATA_FIELDS_SIZE 12
#define PACKET_SIZE (PACKET_HEADER_FIELDS_SIZE + PACKET_DATA_FIELDS_SIZE)

#define SYNC_BYTE 0xAAU
#define PACKET_DATA_FIELD_LEN_OFFSET_1 4
#define PACKET_DATA_FIELD_LEN_OFFSET_2 5
#define MESSAGE_QUEUE_NAME "/mq-acquire-process"
#define MESSAGE_QUEUE_PRIORITY 3

static mqd_t transfer_mq;

static int ipc_init(void);
static int socket_init(int *);
static void *socketConnectionHandler(void *);
static inline void forwardPacket(uint8_t *, size_t);
static ssize_t socket_recv(int, void *, size_t);

int main() {
    openlog(__FILE__, LOG_PID | LOG_CONS, LOG_DAEMON);

    if (daemon_init() == -1) {
        closelog();
        return -1;
    }

    if (signals_init() == -1) {
        closelog();
        return -1;
    }

    if (ipc_init() == -1) {
        closelog();
        return -1;
    }

    int server_fd;

    if (socket_init(&server_fd) == -1) {
        mq_close(transfer_mq);
        closelog();
        return -1;
    }

    if (listen(server_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "listen() failed: %s", strerror(errno));
        mq_close(transfer_mq);
        close(server_fd);
        closelog();
        return -1;
    }

    static unsigned int client_accept_failure_count = 0;

    while (exitRQ == 0) {
        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));

        if (client_fd == NULL) {
            syslog(LOG_ERR, "malloc() Failed: %s", strerror(errno));
            break;
        }

        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        if (*client_fd == -1) {
            free(client_fd);

            if (errno == EINTR) {
                if (exitRQ) {
                    break;
                }

                continue;
            }

            syslog(LOG_ERR, "accept() Failed (%u): %s", client_accept_failure_count, strerror(errno));

            if (++client_accept_failure_count >= CLIENT_ACCEPT_FAILURE_LIMIT_MAX) {
                syslog(LOG_CRIT, "Critical Process Error: Multiple Client accept() Failures: Process Terminating");
                break;
            }

            continue;
        }

        client_accept_failure_count = 0;
        syslog(LOG_INFO, "Client %d Connection Accepted", *client_fd);

        pthread_t threadConnection;

        if (pthread_create(&threadConnection, NULL, socketConnectionHandler, client_fd) != 0) {
            syslog(LOG_ERR, "pthread_create() Failed");
            close(*client_fd);
            free(client_fd);
            continue;
        }

        pthread_detach(threadConnection);
    }

    mq_close(transfer_mq);
    close(server_fd);
    closelog();

    return 0;
}

static void *socketConnectionHandler(void *arg) {
    int client_fd = *(int *)arg;
    uint8_t msg_buffer[PACKET_SIZE];

    free(arg);

    while (!exitRQ) {
        ssize_t bytes_read = socket_recv(client_fd, msg_buffer, PACKET_HEADER_FIELDS_SIZE);

        if (bytes_read == 0) {
            syslog(LOG_INFO, "Client %d Disconnected", client_fd);
            break;
        }

        if (bytes_read < 0) {
            syslog(LOG_ERR, "recv(message header) Failed: %s", strerror(errno));
            break;
        }

        syslog(LOG_INFO, "Header received: %02X %02X %02X %02X %02X %02X", msg_buffer[0], msg_buffer[1], msg_buffer[2], msg_buffer[3], msg_buffer[4], msg_buffer[5]);

        if (msg_buffer[0] != (uint8_t)SYNC_BYTE) {
            syslog(LOG_WARNING, "Invalid Synchronization Byte from Client %d", client_fd);
            break;
        }

        uint16_t msg_data_len = (uint16_t)msg_buffer[PACKET_DATA_FIELD_LEN_OFFSET_1] | ((uint16_t)msg_buffer[PACKET_DATA_FIELD_LEN_OFFSET_2] << 8);

        if (msg_data_len > PACKET_DATA_FIELDS_SIZE) {
            syslog(LOG_WARNING, "Packet Length Exceeds Maximum Permissible Length = %hu", msg_data_len);
            break;
        }

        bytes_read = socket_recv(client_fd, &msg_buffer[PACKET_HEADER_FIELDS_SIZE], msg_data_len);

        if (bytes_read == 0) {
            syslog(LOG_INFO, "Client %d Disconnected", client_fd);
            break;
        }

        if (bytes_read < 0) {
            syslog(LOG_ERR, "recv(message data) Failed: %s", strerror(errno));
            break;
        }

        syslog(LOG_INFO, "Received packet Type=%02X ID=%u Length=%u", msg_buffer[1], (unsigned int)((uint16_t)msg_buffer[2] | ((uint16_t)msg_buffer[3] << 8)), (unsigned int)msg_data_len);

        forwardPacket(msg_buffer, (size_t)(PACKET_HEADER_FIELDS_SIZE + msg_data_len));
    }

    close(client_fd);

    return NULL;
}

static ssize_t socket_recv(int fd, void *buffer, size_t len) {
    size_t total = 0;

    while (total < len) {
        ssize_t bytes_read = recv(fd, (uint8_t *)buffer + total, len - total, 0);

        if (bytes_read == 0) {
            return 0;
        }

        if (bytes_read < 0) {
            if (errno == EINTR) {
                if (exitRQ) {
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

static inline void forwardPacket(uint8_t *packet, size_t len) {
    if (mq_send(transfer_mq, (const char *)packet, len, MESSAGE_QUEUE_PRIORITY) == -1) {
        syslog(LOG_ERR, "mq_send() Failed: %s", strerror(errno));
        return;
    }

    syslog(LOG_INFO, "Packet forwarded to processing queue");
}

static int ipc_init(void) {
    struct mq_attr attr = {0};

    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = PACKET_SIZE;
    attr.mq_curmsgs = 0;

    transfer_mq = mq_open(MESSAGE_QUEUE_NAME, O_WRONLY | O_CREAT, 0644, &attr);

    if (transfer_mq == (mqd_t)-1) {
        syslog(LOG_ERR, "mq_open() Failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int socket_init(int *server_fd) {
    struct addrinfo serverHints = {0};
    struct addrinfo *serverInfo = NULL;
    int status;
    int opt = 1;

    *server_fd = -1;

    serverHints.ai_family = AF_INET;
    serverHints.ai_socktype = SOCK_STREAM;
    serverHints.ai_flags = AI_PASSIVE;
    serverHints.ai_protocol = IPPROTO_TCP;

    status = getaddrinfo(NULL, PORT, &serverHints, &serverInfo);

    if (status != 0) {
        syslog(LOG_ERR, "getaddrinfo() Failed: %s", gai_strerror(status));
        return -1;
    }

    *server_fd = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol);

    if (*server_fd == -1) {
        syslog(LOG_ERR, "socket() Failed: %s", strerror(errno));
        freeaddrinfo(serverInfo);
        return -1;
    }

    if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "setsockopt() Failed: %s", strerror(errno));
        close(*server_fd);
        freeaddrinfo(serverInfo);
        return -1;
    }

    if (bind(*server_fd, serverInfo->ai_addr, serverInfo->ai_addrlen) == -1) {
        syslog(LOG_ERR, "bind() Failed: %s", strerror(errno));
        close(*server_fd);
        freeaddrinfo(serverInfo);
        return -1;
    }

    freeaddrinfo(serverInfo);

    return 0;
}