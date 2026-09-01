
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

static int connect_to_server(const char *ip, int port) {
    int sockfd;
    struct sockaddr_in6 server_addr;

    sockfd = socket(AF_INET6, SOCK_STREAM, 0);

    if (sockfd == -1) {
        perror("socket");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_port = htons((uint16_t)port);

    if (inet_pton(AF_INET6, ip, &server_addr.sin6_addr) != 1) {
        perror("inet_pton");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

static int send_all(int sockfd, const void *buffer, size_t length) {
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t bytes_sent;

        bytes_sent = send(sockfd, (const uint8_t *)buffer + total_sent, length - total_sent, 0);

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

static int send_get_id(int sockfd, uint16_t id) {
    uint8_t command[4];

    command[0] = SYNC_BYTE;
    command[1] = SELECTION_BYTE_GETID;
    memcpy(&command[2], &id, sizeof(id));

    return send_all(sockfd, command, sizeof(command));
}

static ssize_t receive_response(int sockfd, char *buffer, size_t buffer_size) {
    ssize_t bytes_received;

    bytes_received = recv(sockfd, buffer, buffer_size - 1, 0);

    if (bytes_received == -1) {
        if (errno == EINTR) {
            return 0;
        }

        perror("recv");
        return -1;
    }

    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
    }

    return bytes_received;
}

static int parse_latency_response(const char *response, int32_t *sent_seconds, int32_t *sent_microseconds) {
    const char *power;
    const char *current;

    power = strstr(response, "Power:");
    current = strstr(response, "Current:");

    if (power == NULL || current == NULL) {
        return -1;
    }

    if (sscanf(power, "Power: %d", sent_seconds) != 1) {
        return -1;
    }

    if (sscanf(current, "Current: %d", sent_microseconds) != 1) {
        return -1;
    }

    return 0;
}

static void log_latency(FILE *file, int32_t sent_seconds, int32_t sent_microseconds) {
    struct timespec now;
    uint32_t current_seconds;
    uint32_t current_microseconds;
    int64_t sent_time_us;
    int64_t current_time_us;
    int64_t latency_us;

    clock_gettime(CLOCK_REALTIME, &now);

    current_seconds = (uint32_t)((now.tv_sec % 86400 + 86400) % 86400);
    current_microseconds = (uint32_t)(now.tv_nsec / 1000);

    sent_time_us = (int64_t)sent_seconds * 1000000LL + sent_microseconds;
    current_time_us = (int64_t)current_seconds * 1000000LL + current_microseconds;

    latency_us = current_time_us - sent_time_us;

    if (latency_us < 0) {
        latency_us += 86400LL * 1000000LL;
    }

    fprintf(file, "%d,%d,%u,%u,%lld\n",
            sent_seconds,
            sent_microseconds,
            current_seconds,
            current_microseconds,
            (long long)latency_us);

    fflush(file);

    printf("Latency: %lld us (%.3f ms)\n",
           (long long)latency_us,
           (double)latency_us / 1000.0);
}

static void *latency_receiver_thread(void *arg) {
    receiver_args_t *args = arg;
    int sockfd;
    FILE *latency_file;

    sockfd = connect_to_server(args->server_ip, args->server_port);

    if (sockfd == -1) {
        return NULL;
    }

    latency_file = fopen(LATENCY_LOG_FILE, "w");

    if (latency_file == NULL) {
        perror("fopen");
        close(sockfd);
        return NULL;
    }

    fprintf(latency_file, "sent_seconds,sent_microseconds,current_seconds,current_microseconds,latency_microseconds\n");

    printf("Latency receiver started\n");

    while (1) {
        char buffer[TELEMETRY_BUFFER_SIZE];
        ssize_t bytes_received;
        int32_t sent_seconds;
        int32_t sent_microseconds;

        if (send_get_id(sockfd, LATENCY_SENSOR_ID) == -1) {
            fclose(latency_file);
            close(sockfd);
            return NULL;
        }

        bytes_received = receive_response(sockfd, buffer, sizeof(buffer));

        if (bytes_received <= 0) {
            fclose(latency_file);
            close(sockfd);
            return NULL;
        }

        if (parse_latency_response(buffer, &sent_seconds, &sent_microseconds) == 0) {
            log_latency(latency_file, sent_seconds, sent_microseconds);
        }

        usleep(1000);
    }

    fclose(latency_file);
    close(sockfd);

    return NULL;
}

static int send_user_command(int sockfd, const char *command) {
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

    if (strncmp(command, "GET ID ", 7) == 0) {
        id = (uint16_t)strtoul(command + 7, NULL, 10);

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

int main(int argc, char *argv[]) {
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

        bytes_received = receive_response(sockfd, buffer, sizeof(buffer));

        if (bytes_received > 0) {
            printf("%s\n", buffer);
        }

        close(sockfd);
    }

    pthread_cancel(latency_thread);
    pthread_join(latency_thread, NULL);

    return EXIT_SUCCESS;
}
