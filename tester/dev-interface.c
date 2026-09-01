
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>

#define SYNC_BYTE 0xAA
#define SELECTION_BYTE_GETALL 0xF6
#define SELECTION_BYTE_GETTYPE 0xF7
#define SELECTION_BYTE_GETID 0xFB
#define SELECTION_BYTE_GETONLINE 0xFD
#define SELECTION_BYTE_GETOFFLINE 0xFE

#define LATENCY_SENSOR_ID 65000
#define TELEMETRY_BUFFER_SIZE 4096
#define LATENCY_LOG_FILE "latency-log.csv"

typedef struct {
    const char *server_ip;
    int server_port;
} receiver_args_t;

static int connect_to_server(const char *ip, int port)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp;
    char port_string[16];
    int sockfd = -1;
    int status;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_string, sizeof(port_string), "%d", port);

    status = getaddrinfo(ip, port_string, &hints, &result);

    if (status != 0) {
        fprintf(stderr, "getaddrinfo() failed: %s\n", gai_strerror(status));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (sockfd == -1) {
            continue;
        }

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);

    if (sockfd == -1) {
        perror("connect");
    }

    return sockfd;
}

static int send_all(int sockfd, const void *buffer, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t bytes_sent;

        bytes_sent = send(sockfd, (const uint8_t *)buffer + total_sent, length - total_sent, MSG_NOSIGNAL);

        if (bytes_sent == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("send");
            return -1;
        }

        if (bytes_sent == 0) {
            return -1;
        }

        total_sent += (size_t)bytes_sent;
    }

    return 0;
}

static int send_get_id(int sockfd, uint16_t id)
{
    uint8_t command[4];

    command[0] = SYNC_BYTE;
    command[1] = SELECTION_BYTE_GETID;
    memcpy(&command[2], &id, sizeof(id));

    return send_all(sockfd, command, sizeof(command));
}

static ssize_t receive_response(int sockfd, char *buffer, size_t buffer_size)
{
    ssize_t bytes_received;

    do {
        bytes_received = recv(sockfd, buffer, buffer_size - 1, 0);
    } while (bytes_received == -1 && errno == EINTR);

    if (bytes_received == -1) {
        perror("recv");
        return -1;
    }

    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
    }

    return bytes_received;
}

static int parse_latency_response(const char *response, int64_t *sent_seconds, int64_t *sent_nanoseconds)
{
    if(sscanf(response, "LATENCY: %lld %lld", (long long *)sent_seconds, (long long *)sent_nanoseconds) != 2) {
        return -1;
    }

    return 0;
}

static void log_latency(FILE *file, int64_t sent_seconds, int64_t sent_nanoseconds)
{
    struct timespec now;
    int64_t sent_time_ns;
    int64_t current_time_ns;
    int64_t latency_ns;

    if(clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return;
    }

    sent_time_ns = sent_seconds * 1000000000LL + sent_nanoseconds;
    current_time_ns = (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
    latency_ns = current_time_ns - sent_time_ns;

    fprintf(file, "%lld,%lld,%lld,%ld,%lld\n", (long long)sent_seconds, (long long)sent_nanoseconds, (long long)now.tv_sec, now.tv_nsec, (long long)latency_ns);
    fflush(file);

    printf("Latency: %lld ns (%.3f ms)\n", (long long)latency_ns, (double)latency_ns / 1000000.0);
}

static void *latency_receiver_thread(void *arg)
{
    receiver_args_t *args = arg;
    int sockfd;
    FILE *latency_file;

    sockfd = connect_to_server(args->server_ip, args->server_port);

    if(sockfd == -1) {
        return NULL;
    }

    latency_file = fopen(LATENCY_LOG_FILE, "w");

    if(latency_file == NULL) {
        perror("fopen");
        close(sockfd);
        return NULL;
    }

    fprintf(latency_file, "sent_seconds,sent_nanoseconds,current_seconds,current_nanoseconds,latency_nanoseconds\n");
    printf("Latency receiver started\n");

    while(1) {
        char buffer[TELEMETRY_BUFFER_SIZE];
        ssize_t bytes_received;
        int64_t sent_seconds;
        int64_t sent_nanoseconds;

        if(send_get_id(sockfd, LATENCY_SENSOR_ID) == -1) {
            fclose(latency_file);
            close(sockfd);
            return NULL;
        }

        bytes_received = receive_response(sockfd, buffer, sizeof(buffer));

        if(bytes_received <= 0) {
            fclose(latency_file);
            close(sockfd);
            return NULL;
        }

        if(parse_latency_response(buffer, &sent_seconds, &sent_nanoseconds) == 0) {
            log_latency(latency_file, sent_seconds, sent_nanoseconds);
        }

        usleep(1000);
    }
}

static int send_user_command(int sockfd, const char *command)
{
    uint8_t packet[4];
    uint16_t id;
    uint8_t type;

    if (strcmp(command, "GET ALL\n") == 0) {
        packet[0] = SYNC_BYTE;
        packet[1] = SELECTION_BYTE_GETALL;
        return send_all(sockfd, packet, 2);
    }

    if (strcmp(command, "GET ONLINE\n") == 0) {
        packet[0] = SYNC_BYTE;
        packet[1] = SELECTION_BYTE_GETONLINE;
        return send_all(sockfd, packet, 2);
    }

    if (strcmp(command, "GET OFFLINE\n") == 0) {
        packet[0] = SYNC_BYTE;
        packet[1] = SELECTION_BYTE_GETOFFLINE;
        return send_all(sockfd, packet, 2);
    }

    if(strncmp(command, "GET ID ", 7) == 0) {
        char *endptr;
        unsigned long value = strtoul(command + 7, &endptr, 16);

        if(endptr == command + 7 || value > UINT16_MAX) {
            fprintf(stderr, "Invalid hexadecimal sensor ID\n");
            return -1;
        }

        id = (uint16_t)value;

        packet[0] = SYNC_BYTE;
        packet[1] = SELECTION_BYTE_GETID;
        memcpy(&packet[2], &id, sizeof(id));

        return send_all(sockfd, packet, 4);
    }

    if (strcmp(command, "GET TYPE POWCURRVOLT\n") == 0) {
        type = 0x2B;
    }
    else if (strcmp(command, "GET TYPE TOR\n") == 0) {
        type = 0x4B;
    }
    else if (strcmp(command, "GET TYPE TEMPRESS\n") == 0) {
        type = 0x1B;
    }
    else if (strcmp(command, "GET TYPE PROX\n") == 0) {
        type = 0x8B;
    }
    else {
        fprintf(stderr, "Invalid command\n");
        return -1;
    }

    packet[0] = SYNC_BYTE;
    packet[1] = SELECTION_BYTE_GETTYPE;
    packet[2] = type;

    return send_all(sockfd, packet, 3);
}

int main(int argc, char *argv[])
{
    const char *server_ip;
    int server_port;
    receiver_args_t args;
    pthread_t latency_thread;
    char command[256];

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <QEMU_IP> <TELEMETRY_PORT>\n", argv[0]);
        return EXIT_FAILURE;
    }

    server_ip = argv[1];
    server_port = atoi(argv[2]);

    args.server_ip = server_ip;
    args.server_port = server_port;

    if (pthread_create(&latency_thread, NULL, latency_receiver_thread, &args) != 0) {
        perror("pthread_create");
        return EXIT_FAILURE;
    }

    printf("Commands:\n");
    printf("GET ALL\n");
    printf("GET TYPE POWCURRVOLT\n");
    printf("GET TYPE TOR\n");
    printf("GET TYPE TEMPRESS\n");
    printf("GET TYPE PROX\n");
    printf("GET ONLINE\n");
    printf("GET OFFLINE\n");
    printf("GET ID <Sensor_ID>\n");

    while (1) {
        int sockfd;
        char buffer[TELEMETRY_BUFFER_SIZE];
        ssize_t bytes_received;

        printf("> ");

        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }

        sockfd = connect_to_server(server_ip, server_port);

        if (sockfd == -1) {
            continue;
        }

        if (send_user_command(sockfd, command) == -1) {
            close(sockfd);
            continue;
        }

        while((bytes_received = receive_response(sockfd, buffer, sizeof(buffer))) > 0) {
            printf("%s", buffer);
        }

        if(bytes_received < 0) {
            close(sockfd);
            continue;
        }

        close(sockfd);
    }

    pthread_cancel(latency_thread);
    pthread_join(latency_thread, NULL);

    return EXIT_SUCCESS;
}
