
#include "sense.h"
#include "sense_utils.h"
#include "process_init.h"
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define PORT "9196"
#define BACKLOG 10
#define CLIENT_ACCEPT_FAILURE_LIMIT_MAX 16
#define RX_MQ_NAME "/mq-process-telemetry"

#define SYNC_BYTE 0xAAU
#define SYNC_BYTE_SIZE 1
#define SELECTION_BYTE_SIZE 1
#define SELECTION_BYTE_GETALL 0xF6
#define SELECTION_BYTE_GETTYPE 0xF7
#define SELECTION_BYTE_GETID 0xFB
#define SELECTION_BYTE_GETONLINE 0xFD
#define SELECTION_BYTE_GETOFFLINE 0xFE
#define ID_BYTE_SIZE 2
#define TYPE_BYTE_SIZE 1
#define TRANSMIT_BUFFER_SIZE 100

static mqd_t rx_process_mq;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

static Sensor_t *sensor_registry = NULL;
static volatile size_t sensor_count = 0UL;
static size_t sensor_reallocation = 0UL;
static const size_t sensor_allocation_count = 16UL;
static const double sensor_activity_time = SENSOR_INACTIVE_SECS_MAX;

static int socket_init(int*);
static int ipc_init(void);
static ssize_t receiveFrame(Sensor_t*);
static int userInterface_init(void);
static int sensorWatchdog_init(void);
static void* WDT_SensorHandler(void*);
static inline double elapsedSeconds(sensor_timespec_t, sensor_timespec_t);
static void* socketConnectionHandler(void*);
static ssize_t socket_recv(int, void*, size_t);
static int giveSensor(Sensor_t, int);
static int giveSensorAll_Type(uint8_t, int);
static int giveSensorAll_Online(int);
static int giveSensorAll_Offline(int);
static int giveSensorAll(int);

int main() {
    openlog(__FILE__, LOG_PID | LOG_CONS, LOG_DAEMON);

    if(daemon_init() == -1) {
        pthread_mutex_destroy(&counter_mutex);
        closelog();
        return -1;
    }
    if(signals_init() == -1) {
        pthread_mutex_destroy(&counter_mutex);
        closelog();
        return -1;
    }
    if(ipc_init() == -1) {
        pthread_mutex_destroy(&counter_mutex);
        closelog();
        return -1;
    }
    if(sensorWatchdog() == -1) {
        pthread_mutex_destroy(&counter_mutex);
        closelog();
        return -1;
    }
    if(userInterface_init() == -1) {
        pthread_mutex_destroy(&counter_mutex);
        closelog();
        return -1;
    }

    size_t sensor_size = sizeof(Sensor_t);
    sensor_registry = (Sensor_t*)malloc(sensor_size*sensor_allocation_count);
    if(sensor_registry == NULL) {
        syslog(LOG_ERR, "malloc() Failed");
        mq_close(rx_process_mq);
        unlink(RX_MQ_NAME);
        pthread_mutex_destroy(&counter_mutex);
        closelog();
        return -1;
    }

    // Infinite Loop: main Thread
    while(!exitRQ) {
        Sensor_t data_frame;

        if(sensor_count >= sensor_allocation_count && sensor_allocation_count*sensor_reallocation < (size_t)SENSORS_LIMIT_MAX) {
            Sensor_t *new_registry = realloc(sensor_registry, sensor_size*sensor_reallocation);
            if(new_registry == NULL) {
                syslog(LOG_ERR, "malloc() Failed");
                free(sensor_registry);
                mq_close(rx_process_mq);
                unlink(RX_MQ_NAME);
                pthread_mutex_destroy(&counter_mutex);
                closelog();
                return -1;
            }
            sensor_registry = new_registry;
            sensor_reallocation++;
        }
        else if(sensor_allocation_count*sensor_reallocation >= (size_t)SENSORS_LIMIT_MAX) {
            syslog (LOG_ERR, "Max Sensor Registry Size Exceeded");
            free(sensor_registry);
            mq_close(rx_process_mq);
            unlink(RX_MQ_NAME);
            pthread_mutex_destroy(&counter_mutex);
            closelog();
            return -1;
        }

        ssize_t len;
        if((len = receiveFrame(&data_frame)) == -1) {
            free(sensor_registry);
            mq_close(rx_process_mq);
            unlink(RX_MQ_NAME);
            pthread_mutex_destroy(&counter_mutex);
            closelog();
            return -1;
        }
        if(len != (ssize_t)sensor_size) {
            syslog(LOG_ERR, "Incomplete Frame Received");
            continue;
        }

        int8_t new_sensor_flag = 0;
        int sensor_index;
        if((sensor_index = sensorExists(&data_frame, sensor_registry, sensor_count, &new_sensor_flag)) == -1) {
            syslog(LOG_ERR, "Sensor Unsupported");
            free(sensor_registry);
            mq_close(rx_process_mq);
            unlink(RX_MQ_NAME);
            pthread_mutex_destroy(&counter_mutex);
            closelog();
            return -1;
        }
        if(new_sensor_flag == 1) {
            pthread_mutex_lock(&counter_mutex);
            sensor_count++;
            pthread_mutex_unlock(&counter_mutex);
        }
        
        if(updateRegistry(&data_frame, &sensor_registry[sensor_index]) == -1) {
            syslog(LOG_ERR, "Sensor Registry Update Failure");
            free(sensor_registry);
            mq_close(rx_process_mq);
            unlink(RX_MQ_NAME);
            pthread_mutex_destroy(&counter_mutex);
            closelog();
            return -1;
        }
    }

    pthread_mutex_destroy(&counter_mutex);
    free(sensor_registry);
    mq_close(rx_process_mq);
    unlink(RX_MQ_NAME);
    closelog();
    return 0;
}

static int userInterface_init(void) {
    int server_fd;
    if(socket_init(&server_fd) == -1) {
        return -1;
    }

    // Acquire Connections
    if (listen(server_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "listen() failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    // Infinite Loop: userInterface Thread
    static unsigned int client_accept_failure_count = 0;
    while(exitRQ == 0) {
        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int *client_fd = malloc(sizeof(int));
        if(client_fd == NULL) {
            syslog(LOG_ERR, "malloc() Error: %s", strerror(errno));
            close(server_fd);
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
                return -1;
            }
            free(client_fd);
            continue;
        }
        syslog(LOG_INFO, "Client %d Connection Accepted", *client_fd);
        client_accept_failure_count = 0;

        // Thread Designation for Connection
        pthread_t threadConnection;
        if(pthread_create(&threadConnection, NULL, &socketConnectionHandler, client_fd) != 0) {
            close(*client_fd);
            free(client_fd);
            syslog(LOG_ERR, "pthread_create() Failure");
            close(server_fd);
            return -1;
        }
        pthread_detach(threadConnection);
    }

    close(server_fd);
    return 0;
}

static void* socketConnectionHandler(void *arg) {
    int client_fd = *(int*)arg;
    free(arg);

    ssize_t bytes_read;
    while (!exitRQ) {
        uint8_t sync_byte_data = 0;
        while(sync_byte_data != SYNC_BYTE) {
            bytes_read = socket_recv(client_fd, &sync_byte_data, SYNC_BYTE_SIZE);
            if (bytes_read == 0) {
                syslog(LOG_INFO, "Client %d Disconnected", client_fd);
                close(client_fd);
                return NULL;
            }
            if (bytes_read < 0) {
                syslog(LOG_ERR, "recv(sync byte) Failed: %s", strerror(errno));
                close(client_fd);
                return NULL;
            }
        }

        uint8_t selection_byte = 0;
        bytes_read = socket_recv(client_fd, &selection_byte, SELECTION_BYTE_SIZE);
        if (bytes_read == 0) {
            syslog(LOG_INFO, "Client %d Disconnected", client_fd);
            break;
        }
        if (bytes_read < 0) {
            syslog(LOG_ERR, "recv(selection byte) Failed: %s", strerror(errno));
            break;
        }

        if(selection_byte == SELECTION_BYTE_GETID) {
            uint16_t call_id = 0;
            bytes_read = socket_recv(client_fd, &call_id, SELECTION_BYTE_SIZE);
            if (bytes_read == 0) {
                syslog(LOG_INFO, "Client %d Disconnected", client_fd);
                break;
            }
            if (bytes_read < 0) {
                syslog(LOG_ERR, "recv(call id) Failed: %s", strerror(errno));
                break;
            }
            Sensor_t target_sensor = getSensor_ID(call_id, sensor_registry);
            if(socket_send(client_fd, target_sensor) == -1) {
                syslog(LOG_ERR, "send() Failed");
                break;
            }
            if(giveSensor_ID(target_sensor, client_fd) == -1) {
                syslog(LOG_ERR, "send() Failed");
                break;
            }
        }
        else if(selection_byte == SELECTION_BYTE_GETTYPE) {
            uint8_t call_type = 0;
            bytes_read = socket_recv(client_fd, &call_type, SELECTION_BYTE_SIZE);
            if (bytes_read == 0) {
                syslog(LOG_INFO, "Client %d Disconnected", client_fd);
                break;
            }
            if (bytes_read < 0) {
                syslog(LOG_ERR, "recv(call type) Failed: %s", strerror(errno));
                break;
            }
            if(giveSensorAll_Type(call_type, client_fd) == -1) {
                syslog(LOG_ERR, "send() Failed");
                break;
            }
        }
        else {
            if(selection_byte == SELECTION_BYTE_GETONLINE) {
                if(giveSensorAll_Online(client_fd) == -1) {
                    syslog(LOG_ERR, "send() Failed");
                    break;
                }
            }
            else if(selection_byte == SELECTION_BYTE_GETOFFLINE) {
                if(giveSensorAll_Offline(client_fd) == -1) {
                    syslog(LOG_ERR, "send() Failed");
                    break;
                }
            }
            else if(selection_byte == SELECTION_BYTE_GETALL) {
                if(giveSensorAll(client_fd) == -1) {
                    syslog(LOG_ERR, "send() Failed");
                    break;
                }
            }
            else {
                syslog(LOG_ERR, "Unsupported View Option");
                break;
            }
        }
    }

    close(client_fd);
    return NULL;
}

static int giveSensor(Sensor_t target, int socket_fd) {
    ssize_t bytes_sent;

    char timestamp_curr[50];
    getTime(target, timestamp_curr);

    char dataString[50];
    getDataString(target, dataString);
    
    char sendBuffer[TRANSMIT_BUFFER_SIZE] = "";
    if(snprintf(sendBuffer, sizeof(sendBuffer), "%s: %s: %s", timestamp_curr, target.name, dataString) >= (int)sizeof(sendBuffer)) {
        syslog(LOG_ERR, "Log filename too long");
        return -1;
    }

    size_t length = strlen(sendBuffer);
    char *ptr = sendBuffer;
    while (length > 0) {
        bytes_sent = send(socket_fd, ptr, length, 0);
        if (bytes_sent == 0) {
            syslog(LOG_INFO, "Client %d Disconnected", socket_fd);
            break;
        }
        if (bytes_sent < 0) {
            return -1;
        }
        ptr += bytes_sent;
        length -= bytes_sent;
    }

    return 0;
}


static ssize_t socket_recv(int fd, void *buffer, size_t len) {
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
                close(*server_fd);
                freeaddrinfo(serverInfo);
                return -1;
            }
            goto retrySetSockOpt_signalEINTR;
        }
        freeaddrinfo(serverInfo);
        syslog(LOG_ERR, "setsockopt() Failed: %s", strerror(errno));
        close(*server_fd);
        return -1;
    }
retryBind_signalEINTR:
    if (bind(*server_fd, serverInfo->ai_addr, serverInfo->ai_addrlen) != 0) {
        if(errno == EINTR) {
            if(exitRQ) {
                close(*server_fd);
                freeaddrinfo(serverInfo);
                return -1;
            }
            goto retryBind_signalEINTR;
        }
        freeaddrinfo(serverInfo);
        close(*server_fd);
        syslog(LOG_ERR, "bind() Failed: %s", strerror(errno));
        return -1;
    }

    freeaddrinfo(serverInfo);
    serverInfo = NULL;
    return 0;
}

static int sensorWatchdog(void) {
    pthread_t threadConnection;
    if(pthread_create(&threadConnection, NULL, &WDT_SensorHandler, NULL) != 0) {
        syslog(LOG_ERR, "pthread_create() Failure");
        closelog();
        return -1;
    }
    pthread_detach(threadConnection);

    return 0;
}

static void* WDT_SensorHandler(void *arg) {
    while(!exitRQ) {
        pthread_mutex_lock(&counter_mutex);
        size_t count = sensor_count;
        pthread_mutex_unlock(&counter_mutex);

        for(size_t sense_idx = 0; sense_idx < count; sense_idx++) {
            sensor_timespec_t last_seen_time = sensor_registry[sense_idx].last_seen_time;
            sensor_timespec_t curr_time;
            clock_gettime(CLOCK_MONOTONIC, &curr_time);

            double elapsed = elapsedSeconds(last_seen_time, curr_time);
            if(elapsed >= sensor_activity_time) {
                sensor_registry[sense_idx].state = SENSOR_OFFLINE;
            }
            else {
                sensor_registry[sense_idx].state = SENSOR_ONLINE;
            }

            usleep(50000);
        }
    }

    return NULL;
}

static inline double elapsedSeconds(sensor_timespec_t prev, sensor_timespec_t curr) {
    return ((curr.tv_sec - prev.tv_sec) + (curr.tv_nsec - prev.tv_nsec) / (double)1000000000.0);
}

static ssize_t receiveFrame(Sensor_t *frame) {
    size_t frame_size = sizeof(Sensor_t);
    unsigned int priority;
    ssize_t bytes_read;

mq_receive_retry:
    bytes_read = mq_receive(rx_process_mq, frame, frame_size, &priority);
    if(bytes_read == -1) {
        if(errno == EINTR) {
            if(exitRQ) {
                return -1;
            }
            goto mq_receive_retry;
        }
        syslog(LOG_ERR, "mq_receive() Failure: %s", strerror(errno));
        return -1;
    }

    return bytes_read;
}

static int ipc_init(void) {
    rx_process_mq = mq_open(RX_MQ_NAME, O_RDONLY);
    if(rx_process_mq == ((mqd_t) - 1)) {
        return -1;
    }

    return 0;
}
