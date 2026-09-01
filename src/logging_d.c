
#include "sense.h"
#include "sense_utils.h"
#include "process_init.h"
#include <time.h>

#define RX_MQ_NAME "/mq-process-log"
#define FILENAME_SIZE 64

static mqd_t rx_process_mq;
static const char *filepath = "/var/sensorlog/";
static const char *filename_extension = ".csv";

static int ipc_init(void);
static int openLogFile(Sensor_t*, uint8_t*);
static ssize_t receiveFrame(Sensor_t*);

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

    size_t frame_size = sizeof(Sensor_t);
    while(!exitRQ) {
        Sensor_t data_frame;

        ssize_t len;
        if((len = receiveFrame(&data_frame)) == -1) {
            mq_close(rx_process_mq);
            closelog();
            return -1;
        }
        if(len != (ssize_t)frame_size) {
            syslog(LOG_ERR, "Incomplete Frame Received");
            continue;
        }

        uint8_t newfile_flag;
        int fd;
        if((fd = openLogFile(&data_frame, &newfile_flag)) == -1) {
            mq_close(rx_process_mq);
            closelog();
            return -1;
        }

        if(writeFrameData(data_frame.data, data_frame.type, data_frame.timestamp, newfile_flag, fd) == -1) {
            mq_close(rx_process_mq);
            close(fd);
            closelog();
            return -1;
        }

        close(fd);
    }

    mq_close(rx_process_mq);
    closelog();
    return 0;
}

static ssize_t receiveFrame(Sensor_t *frame) {
    size_t frame_size = sizeof(Sensor_t);
    unsigned int priority;
    ssize_t bytes_read;

mq_receive_retry:
    bytes_read = mq_receive(rx_process_mq, (char *)frame, frame_size, &priority);
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

static int openLogFile(Sensor_t *frame, uint8_t *flag)
{
    char filename[FILENAME_SIZE] = "";

    if (snprintf(filename, sizeof(filename), "%s%s%s", filepath, frame->name, filename_extension) >= (int)sizeof(filename))
    {
        syslog(LOG_ERR, "Log filename too long");
        return -1;
    }

    int file_descriptor;

retry_open:
    file_descriptor = open(filename, O_WRONLY | O_APPEND | O_CREAT, 0644);

    if (file_descriptor == -1)
    {
        if (errno == EINTR)
        {
            if (exitRQ)
            {
                return -1;
            }

            goto retry_open;
        }

        syslog(LOG_ERR, "open() Failure: %s", strerror(errno));
        return -1;
    }

    off_t size = lseek(file_descriptor, 0, SEEK_END);

    if (size == -1)
    {
        syslog(LOG_ERR, "lseek() Failed: %s", strerror(errno));
        close(file_descriptor);
        return -1;
    }

    if (size == 0)
    {
        *flag = 1;
    }
    else
    {
        *flag = 0;
    }

    return file_descriptor;
}

static int ipc_init(void) {
    rx_process_mq = mq_open(RX_MQ_NAME, O_RDONLY);
    if(rx_process_mq == ((mqd_t) - 1)) {
        return -1;
    }
    struct mq_attr attr;
    if (mq_getattr(rx_process_mq, &attr) == -1) {
        syslog(LOG_ERR, "mq_getattr failed: %s", strerror(errno));
        return -1;
    }
    if (attr.mq_msgsize < 0 || (size_t)attr.mq_msgsize < sizeof(Sensor_t)) {
        syslog(LOG_ERR, "Acquisition MQ message size too small: %ld", attr.mq_msgsize);
        mq_close(rx_process_mq);
        return -1;
    }

    return 0;
}
