
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
static int forwardFrame(Sensor_t);
static int generateFrame(uint8_t*, Sensor_t*, size_t);
static int receivePacket(uint8_t *buffer);

int main() {
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
        size_t buffer_len = (size_t)PACKET_SIZE;
        uint8_t buffer[buffer_len];

        int packet_size;
        if((packet_size = receivePacket(buffer)) == -1) {
            mq_close(rx_acquire_mq);
            mq_close(tx_log_mq);
            mq_close(tx_telemetry_mq);
            unlink(RX_ACQUIRE_MQ_NAME);
            closelog();
            return -1;
        }
        
        Sensor_t sensor_frame = {0};
        if(generateFrame(buffer, &sensor_frame, (size_t)packet_size) == -1) {
            mq_close(rx_acquire_mq);
            mq_close(tx_log_mq);
            mq_close(tx_telemetry_mq);
            unlink(RX_ACQUIRE_MQ_NAME);
            closelog();
            return -1;
        }

        if(forwardFrame(sensor_frame) == -1) {
            mq_close(rx_acquire_mq);
            mq_close(tx_log_mq);
            mq_close(tx_telemetry_mq);
            unlink(RX_ACQUIRE_MQ_NAME);
            closelog();
            return -1;
        }
    }

    mq_close(rx_acquire_mq);
    mq_close(tx_log_mq);
    mq_close(tx_telemetry_mq);
    unlink(RX_ACQUIRE_MQ_NAME);
    closelog();
    return 0;
}

static int generateFrame(uint8_t *buffer, Sensor_t *frame, size_t size) {
    int iter = 0;

    // Sync Field check
    if(buffer[iter] != (uint8_t)SYNC_FIELD) {
        syslog(LOG_ERR, "Corrupted Packet Received");
        return -1;
    }
    iter++;

    // Type record
    SensorType_e type;
    if((type = findSensorType(buffer[iter])) == SENSOR_TYPE_UNSUPPORTED) {
        syslog(LOG_ERR, "Unsupported Sensor Type");
        return -1;
    }
    frame->type = type;
    iter++;

    // Id record
    uint16_t id = (uint16_t)buffer[iter] | ((uint16_t)buffer[iter + 1] << 8);
    frame->id = id;
    iter += 2;

    // Data Length record
    uint16_t len = (uint16_t)buffer[iter] | ((uint16_t)buffer[iter + 1] << 8);
    if ((size_t)(len + PACKET_HEADER_FIELDS_SIZE) != size) {
        syslog(LOG_ERR, "Packet Length Mismatch");
        return -1;
    }
    if(len > PACKET_DATA_FIELDS_SIZE)
    {
        syslog(LOG_ERR, "Packet Length Exceeds Maximum Permissible Length = %hu", len);
        return -1;
    }
    iter += 2;

    // Name
    nameSensor(frame->name, frame->id, frame->type);

    // Timestamp and Heartbeat collect
    clock_gettime(CLOCK_REALTIME, &frame->timestamp);
    clock_gettime(CLOCK_MONOTONIC, &frame->last_seen_time);

    // Frame type update
    if(len == 0) { // Heartbeat Indication
        frame->frame_type = SENSOR_HEARTBEAT;
        return 0;
    }
    frame->frame_type = SENSOR_DATA;

    // State Update
    frame->state = SENSOR_FRAME_DEFAULT;

    //Data record
    if(recordSensorData(&frame->data, &buffer[iter], len, frame->type) == -1) {
        syslog(LOG_ERR, "Sensor Data Error");
        return -1;
    }

    return 0;
}

static int forwardFrame(Sensor_t frame) {
    if(frame.frame_type == SENSOR_DATA) {
        if(mq_send(tx_log_mq, (const char*)&frame, sizeof(Sensor_t), MQ_PRIORITY) == -1) {
            syslog(LOG_ERR, "mq_send(tx_log_mq) Failed: %s", strerror(errno));
            return -1;
        }
    }
    if(mq_send(tx_telemetry_mq, (const char*)&frame, sizeof(Sensor_t), MQ_PRIORITY) == -1) {
        syslog(LOG_ERR, "mq_send(tx_telemetry_mq) Failed: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

static int receivePacket(uint8_t *buffer) {
    size_t packet_size = (size_t)PACKET_SIZE;
    unsigned int priority;
    ssize_t bytes_read;

mq_receive_retry:
    bytes_read = mq_receive(rx_acquire_mq, buffer, packet_size, &priority);
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
    if((size_t)bytes_read > packet_size) {
        syslog(LOG_ERR, "Packet Length Exceeds Maximum Permissible Length");
        return -1;
    }

    return bytes_read;
}

static int ipc_init(void) {
    rx_acquire_mq = mq_open(RX_ACQUIRE_MQ_NAME, O_RDONLY);
    if(rx_acquire_mq == ((mqd_t) - 1)) {
        return -1;
    }
    tx_log_mq = mq_open(TX_LOG_MQ_NAME, O_WRONLY | O_CREAT, 0644, NULL);
    if(tx_log_mq == ((mqd_t) - 1)) {
        mq_close(rx_acquire_mq);
        mq_unlink(RX_ACQUIRE_MQ_NAME);
        return -1;
    }
    tx_telemetry_mq = mq_open(TX_TELEMETRY_MQ_NAME, O_WRONLY | O_CREAT, 0644, NULL);
    if(tx_telemetry_mq == ((mqd_t) - 1)) {
        mq_close(rx_acquire_mq);
        mq_close(tx_log_mq);
        mq_unlink(RX_ACQUIRE_MQ_NAME);
        return -1;
    }

    return 0;
}
