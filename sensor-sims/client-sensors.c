
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

#define LATENCY_SENSOR_ID 65000
#define LATENCY_SENSOR_TYPE 0x2B

typedef struct {
    const char *server_ip;
    int server_port;
    const char *data_file;
    int thread_id;
    int is_latency_thread;
} thread_args_t;

static int connect_to_server(const char *ip, int port) {
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

static int send_all(int sockfd, const uint8_t *buffer, size_t length) {
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t bytes_sent = send(sockfd, buffer + total_sent, length - total_sent, 0);
        if (bytes_sent == -1) {
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

static size_t build_packet(uint8_t type, uint16_t id, const uint8_t *data, uint16_t data_length, uint8_t *packet) {
    packet[0] = SYNC_FIELD;
    packet[1] = type;
    packet[2] = (uint8_t)(id & 0xFF);
    packet[3] = (uint8_t)((id >> 8) & 0xFF);
    packet[4] = (uint8_t)(data_length & 0xFF);
    packet[5] = (uint8_t)((data_length >> 8) & 0xFF);

    memcpy(&packet[PACKET_HEADER_SIZE], data, data_length);
    return PACKET_HEADER_SIZE + data_length;
}

static void *normal_client_thread(void *arg) {
    thread_args_t *args = arg;
    FILE *file;
    int sockfd;
    char line[256];

    sockfd = connect_to_server(args->server_ip, args->server_port);
    if (sockfd == -1) {
        return NULL;
    }

    file = fopen(args->data_file, "r");
    if (file == NULL) {
        perror("fopen");
        close(sockfd);
        return NULL;
    }
    printf("Normal client %d started\n", args->thread_id);

    while (1) {
        rewind(file);

        while (fgets(line, sizeof(line), file) != NULL) {
            uint8_t packet[MAX_PACKET_SIZE];
            uint8_t data[MAX_DATA_SIZE];
            unsigned int type;
            unsigned int id;
            unsigned int byte;
            uint16_t data_length = 0;
            char *token;

            if (line[0] == '\n' || line[0] == '#') {
                continue;
            }

            token = strtok(line, " \t\n");
            if (token == NULL) {
                continue;
            }
            type = (unsigned int)strtoul(token, NULL, 16);

            token = strtok(NULL, " \t\n");
            if (token == NULL) {
                continue;
            }
            id = (unsigned int)strtoul(token, NULL, 10);

            while (data_length < MAX_DATA_SIZE) {
                token = strtok(NULL, " \t\n");
                if (token == NULL) {
                    break;
                }

                byte = (unsigned int)strtoul(token, NULL, 16);
                data[data_length] = (uint8_t)byte;
                data_length++;
            }

            size_t packet_length;
            packet_length = build_packet((uint8_t)type, (uint16_t)id, data, data_length, packet);


            if (send_all(sockfd, packet, packet_length) == -1) {
                fclose(file);
                close(sockfd);
                return NULL;
            }
            printf("Normal client %d sent: Type=%02X ID=%u Length=%u\n", args->thread_id, type, id, data_length);

            usleep(100000);
        }
    }

    fclose(file);
    close(sockfd);
    return NULL;
}

static void *latency_client_thread(void *arg) {
    thread_args_t *args = arg;
    int sockfd = connect_to_server(args->server_ip, args->server_port);
    if (sockfd == -1) {
        return NULL;
    }

    printf("Latency client started\n");

    while (1) {
        struct timespec now;
        struct tm local_time;
        uint8_t data[12];
        uint8_t packet[MAX_PACKET_SIZE];
        uint32_t microseconds;
        uint32_t seconds_since_midnight;
        size_t packet_length;

        clock_gettime(CLOCK_REALTIME, &now);
        localtime_r(&now.tv_sec, &local_time);

        microseconds = (uint32_t)(now.tv_nsec / 1000);
        seconds_since_midnight = (uint32_t)(local_time.tm_hour * 3600 + local_time.tm_min * 60 + local_time.tm_sec);

        data[0] = (uint8_t)(seconds_since_midnight & 0xFF);
        data[1] = (uint8_t)((seconds_since_midnight >> 8) & 0xFF);
        data[2] = (uint8_t)((seconds_since_midnight >> 16) & 0xFF);
        data[3] = (uint8_t)((seconds_since_midnight >> 24) & 0xFF);

        data[4] = (uint8_t)(microseconds & 0xFF);
        data[5] = (uint8_t)((microseconds >> 8) & 0xFF);
        data[6] = (uint8_t)((microseconds >> 16) & 0xFF);
        data[7] = (uint8_t)((microseconds >> 24) & 0xFF);

        data[8] = 0;
        data[9] = 0;
        data[10] = 0;
        data[11] = 0;

        packet_length = build_packet(LATENCY_SENSOR_TYPE, LATENCY_SENSOR_ID, data, sizeof(data), packet);

        printf("Latency packet sent: ID=%d time=%02d:%02d:%02d.%06u\n", LATENCY_SENSOR_ID, local_time.tm_hour, local_time.tm_min, local_time.tm_sec, microseconds);

        if (send_all(sockfd, packet, packet_length) == -1) {
            close(sockfd);
            return NULL;
        }

        sleep(1);
    }

    close(sockfd);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <QEMU_IP> " "<PORT> " "<DATA_FILE> " "<NUMBER_OF_CLIENTS>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    const char *data_file = argv[3];
    int number_of_clients = atoi(argv[4]);

    if (number_of_clients < 1) {
        fprintf(stderr, "NUMBER_OF_CLIENTS must be at least 1\n");
        return EXIT_FAILURE;
    }


    pthread_t *threads;
    thread_args_t *args;
    threads = calloc((size_t)number_of_clients, sizeof(pthread_t));
    args = calloc((size_t)number_of_clients, sizeof(thread_args_t));


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
            continue;
        }
    }

    for (int i = 0; i < number_of_clients; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    free(args);
    return EXIT_SUCCESS;
}
