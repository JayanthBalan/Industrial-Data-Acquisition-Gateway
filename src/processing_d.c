#include "sense.h"
#include "sense_utils.h"
#include "process_init.h"
#include <time.h>

#define PACKET_HEADER_FIELDS_SIZE 6
#define PACKET_DATA_FIELDS_SIZE 12
#define PACKET_SIZE (PACKET_DATA_FIELDS_SIZE + PACKET_HEADER_FIELDS_SIZE)

#define SYNC_FIELD 0xAAU
#define RX_ACQUIRE_MQ_NAME "/mq-acquire-process"
#define TX_LOG_MQ_NAME "/mq-process-log"
#define TX_TELEMETRY_MQ_NAME "/mq-process-telemetry"
#define MQ_PRIORITY 3

static mqd_t rx_acquire_mq;
static mqd_t tx_log_mq;
static mqd_t tx_telemetry_mq;

static int ipc_init(void);
static int forwardFrame(const Sensor_t *frame);
static int generateFrame(const uint8_t *buffer, Sensor_t *frame, size_t size);
static ssize_t receivePacket(uint8_t *buffer);

int main(void) {
    openlog(__FILE__, LOG_PID | LOG_CONS, LOG_DAEMON);

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

    while(!exitRQ) {
        uint8_t buffer[PACKET_SIZE];
        ssize_t packet_size = receivePacket(buffer);

        if(packet_size == -1) {
            if(exitRQ) {
                break;
            }
            continue;
        }

        Sensor_t sensor_frame = {0};

        if(generateFrame(buffer, &sensor_frame, (size_t)packet_size) == -1) {
            continue;
        }

        if(forwardFrame(&sensor_frame) == -1) {
            continue;
        }
    }

    mq_close(rx_acquire_mq);
    mq_close(tx_log_mq);
    mq_close(tx_telemetry_mq);
    closelog();
    return 0;
}

static int generateFrame(const uint8_t *buffer, Sensor_t *frame, size_t size) {
    if(buffer == NULL || frame == NULL) {
        return -1;
    }

    if(size < PACKET_HEADER_FIELDS_SIZE) {
        syslog(LOG_ERR, "Packet smaller than header");
        return -1;
    }

    if(buffer[0] != (uint8_t)SYNC_FIELD) {
        syslog(LOG_ERR, "Corrupted Packet Received");
        return -1;
    }

    SensorType_e type = findSensorType(buffer[1]);

    if(type == SENSOR_TYPE_UNSUPPORTED) {
        syslog(LOG_ERR, "Unsupported Sensor Type: %02X", buffer[1]);
        return -1;
    }

    uint16_t id = (uint16_t)buffer[2] | ((uint16_t)buffer[3] << 8);
    uint16_t len = (uint16_t)buffer[4] | ((uint16_t)buffer[5] << 8);

    if(len > PACKET_DATA_FIELDS_SIZE) {
        syslog(LOG_ERR, "Packet Length Exceeds Maximum Permissible Length = %hu", len);
        return -1;
    }

    if((size_t)len + PACKET_HEADER_FIELDS_SIZE != size) {
        syslog(LOG_ERR, "Packet Length Mismatch: Header=%hu Received=%zu", len, size);
        return -1;
    }

    frame->type = type;
    frame->id = id;

    nameSensor(frame->name, frame->id, frame->type);

    if(clock_gettime(CLOCK_REALTIME, &frame->timestamp) == -1) {
        syslog(LOG_ERR, "clock_gettime(CLOCK_REALTIME) Failed: %s", strerror(errno));
        return -1;
    }

    if(clock_gettime(CLOCK_MONOTONIC, &frame->last_seen_time) == -1) {
        syslog(LOG_ERR, "clock_gettime(CLOCK_MONOTONIC) Failed: %s", strerror(errno));
        return -1;
    }

    frame->state = SENSOR_FRAME_DEFAULT;

    if(len == 0) {
        frame->frame_type = SENSOR_HEARTBEAT;
        return 0;
    }

    frame->frame_type = SENSOR_DATA;

    if(recordSensorData(&frame->data, &buffer[PACKET_HEADER_FIELDS_SIZE], len, frame->type) == -1) {
        syslog(LOG_ERR, "Sensor Data Error: Type=%02X ID=%hu Length=%hu", buffer[1], id, len);
        return -1;
    }

    return 0;
}

static int forwardFrame(const Sensor_t *frame) {
    if(frame == NULL) {
        return -1;
    }

    if(frame->frame_type == SENSOR_DATA) {
        if(mq_send(tx_log_mq, (const char *)frame, sizeof(*frame), MQ_PRIORITY) == -1) {
            if(errno == EAGAIN) {
                syslog(LOG_WARNING, "Log queue full, dropping frame ID=%hu", frame->id);
            } else {
                syslog(LOG_ERR, "mq_send(tx_log_mq) Failed: %s", strerror(errno));
            }
            return -1;
        }
    }

    if(mq_send(tx_telemetry_mq, (const char *)frame, sizeof(*frame), MQ_PRIORITY) == -1) {
        if(errno == EAGAIN) {
            syslog(LOG_WARNING, "Telemetry queue full, dropping frame ID=%hu", frame->id);
        } else {
            syslog(LOG_ERR, "mq_send(tx_telemetry_mq) Failed: %s", strerror(errno));
        }
        return -1;
    }

    return 0;
}

static ssize_t receivePacket(uint8_t *buffer) {
    unsigned int priority;
    ssize_t bytes_read;

    if(buffer == NULL) {
        return -1;
    }

mq_receive_retry:
    bytes_read = mq_receive(rx_acquire_mq, (char *)buffer, PACKET_SIZE, &priority);

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

    if(bytes_read < (ssize_t)PACKET_HEADER_FIELDS_SIZE) {
        syslog(LOG_ERR, "Packet smaller than header: %zd", bytes_read);
        return -1;
    }

    if(bytes_read > (ssize_t)PACKET_SIZE) {
        syslog(LOG_ERR, "Packet Length Exceeds Maximum Permissible Length");
        return -1;
    }

    return bytes_read;
}

static int ipc_init(void) {
    struct mq_attr attr;
    struct mq_attr tx_attr;

    rx_acquire_mq = mq_open(RX_ACQUIRE_MQ_NAME, O_RDONLY);

    if(rx_acquire_mq == (mqd_t)-1) {
        syslog(LOG_ERR, "mq_open(rx_acquire_mq) Failed: %s", strerror(errno));
        return -1;
    }

    if(mq_getattr(rx_acquire_mq, &attr) == -1) {
        syslog(LOG_ERR, "mq_getattr(rx_acquire_mq) Failed: %s", strerror(errno));
        mq_close(rx_acquire_mq);
        return -1;
    }

    if(attr.mq_msgsize < (long)PACKET_SIZE) {
        syslog(LOG_ERR, "Acquisition MQ message size too small: %ld", attr.mq_msgsize);
        mq_close(rx_acquire_mq);
        return -1;
    }

    memset(&tx_attr, 0, sizeof(tx_attr));
    tx_attr.mq_flags = 0;
    tx_attr.mq_maxmsg = 10;
    tx_attr.mq_msgsize = sizeof(Sensor_t);

    tx_log_mq = mq_open(TX_LOG_MQ_NAME, O_WRONLY | O_CREAT | O_NONBLOCK, 0644, &tx_attr);

    if(tx_log_mq == (mqd_t)-1) {
        syslog(LOG_ERR, "mq_open(tx_log_mq) Failed: %s", strerror(errno));
        mq_close(rx_acquire_mq);
        return -1;
    }

    tx_telemetry_mq = mq_open(TX_TELEMETRY_MQ_NAME, O_WRONLY | O_CREAT | O_NONBLOCK, 0644, &tx_attr);

    if(tx_telemetry_mq == (mqd_t)-1) {
        syslog(LOG_ERR, "mq_open(tx_telemetry_mq) Failed: %s", strerror(errno));
        mq_close(rx_acquire_mq);
        mq_close(tx_log_mq);
        return -1;
    }

    return 0;
}