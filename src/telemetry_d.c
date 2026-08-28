
#include "sense.h"
#include "sense_utils.h"
#include "process_init.h"
#include <time.h>

#define RX_MQ_NAME "/mq-process-telemetry"

static mqd_t rx_process_mq;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

static Sensor_t *sensor_registry = NULL;
static volatile size_t sensor_count = 0UL;
static size_t sensor_reallocation = 0UL;
static const size_t sensor_allocation_count = 16UL;
static const double sensor_activity_time = SENSOR_INACTIVE_SECS_MAX;

static int ipc_init(void);
static ssize_t receiveFrame(Sensor_t*);
static int userInterface_init(void);
static int sensorWatchdog_init(void);
static void* WDT_SensorHandler(void*);
static inline double elapsedSeconds(sensor_timespec_t, sensor_timespec_t);

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
    if(userInterface_init() == -1) {
        pthread_mutex_destroy(&counter_mutex);
        closelog();
        return -1;
    }
    if(sensorWatchdog() == -1) {
        pthread_mutex_destroy(&counter_mutex);
        closelog();
        return -1;
    }
    if(ipc_init() == -1) {
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
