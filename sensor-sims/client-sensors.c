

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

#define SYNC_FIELD 0xAA

#define PACKET_HEADER_SIZE 6
#define MAX_DATA_SIZE 12
#define MAX_PACKET_SIZE (PACKET_HEADER_SIZE + MAX_DATA_SIZE)

#define LATENCY_SENSOR_ID 0xFDE8U
#define LATENCY_SENSOR_TYPE 0x2B

typedef struct {
    const char *server_ip;
    int server_port;
    const char *data_file;
    int thread_id;
    int is_latency_thread;
} thread_args_t;

static int connect_to_server(const char *ip, int port);
static int send_all(int sockfd, const uint8_t *buffer, size_t length);
static size_t build_packet(uint8_t type, uint16_t id, const uint8_t *data, uint16_t data_length, uint8_t *packet);
static void *normal_client_thread(void *arg);
static void *latency_client_thread(void *arg);

int sleep_time;

static int connect_to_server(const char *ip, int port)
{
    int sockfd;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) != 1) {
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

static int send_all(int sockfd, const uint8_t *buffer, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t bytes_sent = send(sockfd, buffer + total_sent, length - total_sent, 0);

        if (bytes_sent == 0) {
            fprintf(stderr, "send() returned 0\n");
            return -1;
        }

        if (bytes_sent < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("send");
            return -1;
        }

        total_sent += (size_t)bytes_sent;
    }

    return 0;
}

static size_t build_packet(uint8_t type, uint16_t id, const uint8_t *data, uint16_t data_length, uint8_t *packet)
{
    packet[0] = SYNC_FIELD;
    packet[1] = type;
    packet[2] = (uint8_t)(id & 0xFF);
    packet[3] = (uint8_t)((id >> 8) & 0xFF);
    packet[4] = (uint8_t)(data_length & 0xFF);
    packet[5] = (uint8_t)((data_length >> 8) & 0xFF);

    if (data_length > 0) {
        memcpy(packet + PACKET_HEADER_SIZE, data, data_length);
    }

    return PACKET_HEADER_SIZE + data_length;
}

static void *normal_client_thread(void *arg)
{
    thread_args_t *args = arg;
    FILE *file;
    int sockfd;
    char line[256];

    sockfd = connect_to_server(args->server_ip, args->server_port);
    if (sockfd == -1) {
        fprintf(stderr, "Normal client %d failed to connect\n", args->thread_id);
        return NULL;
    }

    printf("Normal client %d connected to %s:%d\n", args->thread_id, args->server_ip, args->server_port);

    file = fopen(args->data_file, "r");
    if (file == NULL) {
        perror("fopen");
        close(sockfd);
        return NULL;
    }

    while (1) {
        rewind(file);

        while (fgets(line, sizeof(line), file) != NULL) {
            uint8_t packet[MAX_PACKET_SIZE];
            uint8_t data[MAX_DATA_SIZE];
            unsigned long type_value;
            unsigned long id_value;
            unsigned long byte_value;
            uint16_t data_length = 0;
            size_t packet_length;
            char *token;
            char *endptr;

            token = strtok(line, " \t\r\n");

            if (token == NULL) {
                continue;
            }

            if (token[0] == '#') {
                continue;
            }

            errno = 0;
            type_value = strtoul(token, &endptr, 16);

            if (errno != 0 || *endptr != '\0' || type_value > UINT8_MAX) {
                fprintf(stderr, "Invalid sensor type\n");
                continue;
            }

            token = strtok(NULL, " \t\r\n");
            if (token == NULL) {
                fprintf(stderr, "Missing sensor ID\n");
                continue;
            }

            errno = 0;
            id_value = strtoul(token, &endptr, 10);

            if (errno != 0 || *endptr != '\0' || id_value > UINT16_MAX) {
                fprintf(stderr, "Invalid sensor ID\n");
                continue;
            }

            while (data_length < MAX_DATA_SIZE) {
                token = strtok(NULL, " \t\r\n");

                if (token == NULL) {
                    break;
                }

                errno = 0;
                byte_value = strtoul(token, &endptr, 16);

                if (errno != 0 || *endptr != '\0' ||
                    byte_value > UINT8_MAX) {
                    fprintf(stderr, "Invalid data byte\n");
                    data_length = 0;
                    break;
                }

                data[data_length] = (uint8_t)byte_value;
                data_length++;
            }

            if (token != NULL && data_length == 0) {
                continue;
            }

            packet_length = build_packet((uint8_t)type_value, (uint16_t)id_value, data, data_length, packet);

            if (send_all(sockfd, packet, packet_length) == -1) {
                fprintf(stderr, "Normal client %d connection lost\n", args->thread_id);

                fclose(file);
                close(sockfd);
                return NULL;
            }

            usleep(sleep_time);
        }

        clearerr(file);
    }

    fclose(file);
    close(sockfd);

    return NULL;
}

static void *latency_client_thread(void *arg)
{
    thread_args_t *args = arg;
    int sockfd;

    sockfd = connect_to_server(args->server_ip, args->server_port);

    if(sockfd == -1) {
        fprintf(stderr, "Latency client failed to connect\n");
        return NULL;
    }

    printf("Latency client connected to %s:%d\n", args->server_ip, args->server_port);

    while(1) {
        struct timespec now;
        uint8_t data[12];
        uint8_t packet[MAX_PACKET_SIZE];
        int64_t seconds;
        int32_t nanoseconds;
        size_t packet_length;

        if(clock_gettime(CLOCK_REALTIME, &now) == -1) {
            perror("clock_gettime");
            close(sockfd);
            return NULL;
        }

        seconds = (int64_t)now.tv_sec;
        nanoseconds = (int32_t)now.tv_nsec;

        memcpy(&data[0], &seconds, sizeof(seconds));
        memcpy(&data[8], &nanoseconds, sizeof(nanoseconds));

        packet_length = build_packet(LATENCY_SENSOR_TYPE, LATENCY_SENSOR_ID, data, sizeof(data), packet);

        if(send_all(sockfd, packet, packet_length) == -1) {
            fprintf(stderr, "Latency client connection lost\n");
            close(sockfd);
            return NULL;
        }

        printf("Latency packet sent: ID=0x%04X time=%lld.%09d\n", LATENCY_SENSOR_ID, (long long)seconds, nanoseconds);

        sleep(2);
    }

    close(sockfd);
    return NULL;
}

int main(int argc, char *argv[])
{
    const char *server_ip;
    int server_port;
    const char *data_file;
    int number_of_clients;
    pthread_t *threads;
    thread_args_t *args;

    if (argc != 6) {
        fprintf(stderr, "Usage: %s <QEMU_IP> <PORT> <DATA_FILE> <NUMBER_OF_CLIENTS> <SLEEP_TIME_BTW_PACKET_TRANSMITS>\n", argv[0]);

        return EXIT_FAILURE;
    }

    server_ip = argv[1];
    server_port = atoi(argv[2]);
    data_file = argv[3];
    number_of_clients = atoi(argv[4]);
    sleep_time = atoi(argv[5]);

    if (server_port < 1 || server_port > 65535) {
        fprintf(stderr, "PORT must be between 1 and 65535\n");
        return EXIT_FAILURE;
    }

    if (number_of_clients < 1) {
        fprintf(stderr, "NUMBER_OF_CLIENTS must be at least 1\n");

        return EXIT_FAILURE;
    }

    threads = calloc((size_t)number_of_clients, sizeof(*threads));
    args = calloc((size_t)number_of_clients, sizeof(*args));

    if (threads == NULL || args == NULL) {
        perror("calloc");

        free(threads);
        free(args);

        return EXIT_FAILURE;
    }

    args[0].server_ip = server_ip;
    args[0].server_port = server_port;
    args[0].data_file = data_file;
    args[0].thread_id = 0;
    args[0].is_latency_thread = 1;

    if (pthread_create(&threads[0], NULL, latency_client_thread, &args[0]) != 0) {
        perror("pthread_create");

        free(threads);
        free(args);

        return EXIT_FAILURE;
    }

    for (int i = 1; i < number_of_clients; i++) {
        args[i].server_ip = server_ip;
        args[i].server_port = server_port;
        args[i].data_file = data_file;
        args[i].thread_id = i;
        args[i].is_latency_thread = 0;

        if (pthread_create(&threads[i], NULL, normal_client_thread, &args[i]) != 0) {
            perror("pthread_create");
        }
    }

    for (int i = 0; i < number_of_clients; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    free(args);

    return EXIT_SUCCESS;
}
