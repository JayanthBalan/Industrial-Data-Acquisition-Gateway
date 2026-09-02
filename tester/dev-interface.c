
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
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
#define SELECTION_BYTE_GETTHROUGHPUT 0xFC
#define SELECTION_BYTE_GETONLINE 0xFD
#define SELECTION_BYTE_GETOFFLINE 0xFE

#define LATENCY_SENSOR_ID 65000
#define TELEMETRY_BUFFER_SIZE 4096
#define LATENCY_LOG_FILE "latency-log.csv"

int latency_poll_time;

typedef struct {
    const char *server_ip;
    int server_port;
    pthread_mutex_t *request_mutex;
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

    if(status != 0) {
        fprintf(stderr, "getaddrinfo() failed: %s\n", gai_strerror(status));
        return -1;
    }

    for(rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if(sockfd == -1) {
            continue;
        }

        if(connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);

    if(sockfd == -1) {
        perror("connect");
    }

    return sockfd;
}

static int send_all(int sockfd, const void *buffer, size_t length)
{
    size_t total_sent = 0;

    while(total_sent < length) {
        ssize_t bytes_sent;

        bytes_sent = send(sockfd, (const uint8_t *)buffer + total_sent, length - total_sent, MSG_NOSIGNAL);

        if(bytes_sent < 0) {
            if(errno == EINTR) {
                continue;
            }

            perror("send");
            return -1;
        }

        if(bytes_sent == 0) {
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

static int send_get_throughput(int sockfd)
{
    uint8_t command[2];

    command[0] = SYNC_BYTE;
    command[1] = SELECTION_BYTE_GETTHROUGHPUT;

    return send_all(sockfd, command, sizeof(command));
}

static int parse_latency_response(const char *response, int64_t *sent_seconds, int64_t *sent_nanoseconds)
{
    long seconds;
    long nanoseconds;

    if(sscanf(response, "LATENCY: %ld %ld", &seconds, &nanoseconds) != 2) {
        return -1;
    }

    *sent_seconds = (int64_t)seconds;
    *sent_nanoseconds = (int64_t)nanoseconds;

    return 0;
}

static void log_latency(FILE *file, int64_t sent_seconds, int64_t sent_nanoseconds)
{
    static int64_t previous_seconds = -1;
    static int64_t previous_nanoseconds = -1;
    struct timespec now;
    int64_t sent_time_us;
    int64_t current_time_us;
    int64_t latency_us;

    if(sent_seconds == previous_seconds && sent_nanoseconds == previous_nanoseconds) {
        return;
    }

    previous_seconds = sent_seconds;
    previous_nanoseconds = sent_nanoseconds;

    if(clock_gettime(CLOCK_REALTIME, &now) == -1) {
        return;
    }

    sent_time_us = sent_seconds * 1000000LL + sent_nanoseconds / 1000LL;
    current_time_us = (int64_t)now.tv_sec * 1000000LL + now.tv_nsec / 1000LL;
    latency_us = current_time_us - sent_time_us;

    fprintf(file, "%lld\n", (long long)latency_us);
    fflush(file);
}

static void print_average_latency(void)
{
    FILE *file;
    char line[256];
    int64_t values[5];
    size_t count = 0;
    int64_t sum = 0;

    file = fopen(LATENCY_LOG_FILE, "r");

    if(file == NULL) {
        perror("fopen");
        return;
    }

    while(fgets(line, sizeof(line), file) != NULL) {
        char *endptr;
        long long value;

        if(strncmp(line, "latency_microseconds", 20) == 0) {
            continue;
        }

        errno = 0;

        value = strtoll(line, &endptr, 10);

        if(errno != 0 || endptr == line) {
            continue;
        }

        if(count < 5) {
            values[count++] = (int64_t)value;
        }
        else {
            values[0] = values[1];
            values[1] = values[2];
            values[2] = values[3];
            values[3] = values[4];
            values[4] = (int64_t)value;
        }
    }

    fclose(file);

    if(count == 0) {
        printf("No latency samples available\n");
        return;
    }

    for(size_t i = 0; i < count; i++) {
        sum += values[i];
    }

    printf("Average latency from the last %zu samples: %lld microseconds\n", count, (long long)(sum / (int64_t)count));
}

static int process_line(const char *line, int print_response, FILE *latency_file)
{
    int64_t sent_seconds;
    int64_t sent_nanoseconds;

    if(strcmp(line, "END") == 0) {
        return 1;
    }

    if(print_response) {
        printf("%s\n", line);
    }

    if(latency_file != NULL) {
        if(parse_latency_response(line, &sent_seconds, &sent_nanoseconds) == 0) {
            log_latency(latency_file, sent_seconds, sent_nanoseconds);
        }
    }

    return 0;
}

static int receive_until_end(int sockfd, int print_response, FILE *latency_file)
{
    char buffer[TELEMETRY_BUFFER_SIZE];
    char line[TELEMETRY_BUFFER_SIZE];
    size_t line_length = 0;

    while(1) {
        ssize_t bytes_received;

        bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);

        if(bytes_received == 0) {
            return -1;
        }

        if(bytes_received < 0) {
            if(errno == EINTR) {
                continue;
            }

            perror("recv");
            return -1;
        }

        for(ssize_t i = 0; i < bytes_received; i++) {
            if(buffer[i] == '\n') {
                int result;

                line[line_length] = '\0';

                result = process_line(line, print_response, latency_file);

                line_length = 0;

                if(result == 1) {
                    return 0;
                }
            }
            else {
                if(line_length >= sizeof(line) - 1) {
                    return -1;
                }

                line[line_length++] = buffer[i];
            }
        }
    }
}

static void *latency_receiver_thread(void *arg)
{
    receiver_args_t *args = arg;
    FILE *latency_file;

    latency_file = fopen(LATENCY_LOG_FILE, "w");

    if(latency_file == NULL) {
        perror("fopen");
        return NULL;
    }

    fprintf(latency_file, "latency_microseconds\n");
    fflush(latency_file);

    while(1) {
        int sockfd;

        pthread_mutex_lock(args->request_mutex);

        sockfd = connect_to_server(args->server_ip, args->server_port);

        if(sockfd != -1) {
            if(send_get_id(sockfd, LATENCY_SENSOR_ID) == 0) {
                receive_until_end(sockfd, 0, latency_file);
            }

            close(sockfd);
        }

        pthread_mutex_unlock(args->request_mutex);

        usleep(latency_poll_time);
    }

    return NULL;
}

static int send_user_command(int sockfd, const char *command)
{
    uint8_t packet[4];
    uint16_t id;
    uint8_t type;

    if(strcmp(command, "GET ALL\n") == 0) {
        packet[0] = SYNC_BYTE;
        packet[1] = SELECTION_BYTE_GETALL;

        return send_all(sockfd, packet, 2);
    }

    if(strcmp(command, "GET ONLINE\n") == 0) {
        packet[0] = SYNC_BYTE;
        packet[1] = SELECTION_BYTE_GETONLINE;

        return send_all(sockfd, packet, 2);
    }

    if(strcmp(command, "GET OFFLINE\n") == 0) {
        packet[0] = SYNC_BYTE;
        packet[1] = SELECTION_BYTE_GETOFFLINE;

        return send_all(sockfd, packet, 2);
    }

    if(strncmp(command, "GET ID ", 7) == 0) {
        char *endptr;
        unsigned long value;

        value = strtoul(command + 7, &endptr, 16);

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

    if(strcmp(command, "GET TYPE POWCURRVOLT\n") == 0) {
        type = 0x2B;
    }
    else if(strcmp(command, "GET TYPE TOR\n") == 0) {
        type = 0x4B;
    }
    else if(strcmp(command, "GET TYPE TEMPRESS\n") == 0) {
        type = 0x1B;
    }
    else if(strcmp(command, "GET TYPE PROX\n") == 0) {
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
    pthread_mutex_t request_mutex;
    char command[256];

    if(argc != 4) {
        fprintf(stderr, "Usage: %s <QEMU_IP> <TELEMETRY_PORT>\n", argv[0]);
        return EXIT_FAILURE;
    }

    server_ip = argv[1];
    server_port = atoi(argv[2]);
    latency_poll_time = atoi(argv[3]);

    if(pthread_mutex_init(&request_mutex, NULL) != 0) {
        perror("pthread_mutex_init");
        return EXIT_FAILURE;
    }

    args.server_ip = server_ip;
    args.server_port = server_port;
    args.request_mutex = &request_mutex;

    if(pthread_create(&latency_thread, NULL, latency_receiver_thread, &args) != 0) {
        perror("pthread_create");
        pthread_mutex_destroy(&request_mutex);
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
    printf("GET LATENCY\n");
    printf("GET THROUGHPUT\n");

    while(1) {
        int sockfd;

        printf("> ");
        fflush(stdout);

        if(fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }

        if(strcmp(command, "GET LATENCY\n") == 0) {
            print_average_latency();
            continue;
        }

        pthread_mutex_lock(&request_mutex);

        sockfd = connect_to_server(server_ip, server_port);

        if(sockfd == -1) {
            pthread_mutex_unlock(&request_mutex);
            continue;
        }

        if(strcmp(command, "GET THROUGHPUT\n") == 0) {
            if(send_get_throughput(sockfd) == -1) {
                close(sockfd);
                pthread_mutex_unlock(&request_mutex);
                continue;
            }

            if(receive_until_end(sockfd, 1, NULL) == -1) {
                fprintf(stderr, "Failed to receive throughput data\n");
            }

            close(sockfd);
            pthread_mutex_unlock(&request_mutex);
            continue;
        }

        if(send_user_command(sockfd, command) == -1) {
            close(sockfd);
            pthread_mutex_unlock(&request_mutex);
            continue;
        }

        if(receive_until_end(sockfd, 1, NULL) == -1) {
            fprintf(stderr, "Failed to receive complete response\n");
        }

        close(sockfd);
        pthread_mutex_unlock(&request_mutex);
    }

    pthread_cancel(latency_thread);
    pthread_join(latency_thread, NULL);

    pthread_mutex_destroy(&request_mutex);

    return EXIT_SUCCESS;
}
