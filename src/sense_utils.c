
#include "sense_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define WRITE_BUFFER_SIZE 300
#define INTER_BUF_SIZE 50

const char *SENSOR_NAME_3_TEMPRESS = "TPS";
const char *SENSOR_NAME_4_POWCURRVOLT = "PCVS";
const char *SENSOR_NAME_5_TORQUE = "TORQS";
const char *SENSOR_NAME_5_PROXIMITY = "PROXS";

static inline void measurement_to_string(char *, size_t, int32_t, int);
static int write_all(int, const char *, size_t);

void nameSensor(char *string, uint16_t id, SensorType_e type) {
    const char *name_base;

    if(string == NULL) {
        return;
    }

    switch(type) {
        case SENSOR_TYPE_POWCURRVOLT:
            name_base = SENSOR_NAME_4_POWCURRVOLT;
            break;
        case SENSOR_TYPE_PROXIMITY:
            name_base = SENSOR_NAME_5_PROXIMITY;
            break;
        case SENSOR_TYPE_TEMPPRESS:
            name_base = SENSOR_NAME_3_TEMPRESS;
            break;
        case SENSOR_TYPE_TORQUE:
            name_base = SENSOR_NAME_5_TORQUE;
            break;
        default:
            string[0] = '\0';
            return;
    }

    snprintf(string, SENSOR_NAME_SIZE, "%s_0X%" PRIx16, name_base, id);
}

int recordSensorData(SensorData_u *target, const uint8_t *raw_data, uint16_t byte_size, SensorType_e type) {
    uint16_t idx = 0;
    uint32_t value;

    if(target == NULL || raw_data == NULL) {
        return -1;
    }

    if(type == SENSOR_TYPE_POWCURRVOLT) {
        if(byte_size != 12) {
            return -1;
        }

        value = (uint32_t)raw_data[idx] | ((uint32_t)raw_data[idx + 1] << 8) | ((uint32_t)raw_data[idx + 2] << 16) | ((uint32_t)raw_data[idx + 3] << 24);
        target->powcurrvolt.power = (int32_t)value;
        idx += 4;

        value = (uint32_t)raw_data[idx] | ((uint32_t)raw_data[idx + 1] << 8) | ((uint32_t)raw_data[idx + 2] << 16) | ((uint32_t)raw_data[idx + 3] << 24);
        target->powcurrvolt.current = (int32_t)value;
        idx += 4;

        value = (uint32_t)raw_data[idx] | ((uint32_t)raw_data[idx + 1] << 8) | ((uint32_t)raw_data[idx + 2] << 16) | ((uint32_t)raw_data[idx + 3] << 24);
        target->powcurrvolt.voltage = (int32_t)value;
        idx += 4;
    }
    else if(type == SENSOR_TYPE_TEMPPRESS) {
        if(byte_size != 8) {
            return -1;
        }

        value = (uint32_t)raw_data[idx] | ((uint32_t)raw_data[idx + 1] << 8) | ((uint32_t)raw_data[idx + 2] << 16) | ((uint32_t)raw_data[idx + 3] << 24);
        target->temppress.temperature = (int32_t)value;
        idx += 4;

        value = (uint32_t)raw_data[idx] | ((uint32_t)raw_data[idx + 1] << 8) | ((uint32_t)raw_data[idx + 2] << 16) | ((uint32_t)raw_data[idx + 3] << 24);
        target->temppress.pressure = value;
        idx += 4;
    }
    else if(type == SENSOR_TYPE_PROXIMITY) {
        if(byte_size != 4) {
            return -1;
        }

        value = (uint32_t)raw_data[idx] | ((uint32_t)raw_data[idx + 1] << 8) | ((uint32_t)raw_data[idx + 2] << 16) | ((uint32_t)raw_data[idx + 3] << 24);
        target->prox.proximity = value;
        idx += 4;
    }
    else if(type == SENSOR_TYPE_TORQUE) {
        if(byte_size != 4) {
            return -1;
        }

        value = (uint32_t)raw_data[idx] | ((uint32_t)raw_data[idx + 1] << 8) | ((uint32_t)raw_data[idx + 2] << 16) | ((uint32_t)raw_data[idx + 3] << 24);
        target->tor.torque = (int32_t)value;
        idx += 4;
    }
    else {
        return -1;
    }

    if(idx != byte_size) {
        return -1;
    }

    return 0;
}

int writeFrameData(SensorData_u data, SensorType_e type, sensor_timespec_t stamp, uint8_t flag, int fd) {
    char title_buffer[WRITE_BUFFER_SIZE] = "";
    char write_buffer[WRITE_BUFFER_SIZE] = "";
    char timestamp[INTER_BUF_SIZE] = "";
    char power[INTER_BUF_SIZE] = "";
    char current[INTER_BUF_SIZE] = "";
    char voltage[INTER_BUF_SIZE] = "";
    char temperature[INTER_BUF_SIZE] = "";
    char pressure[INTER_BUF_SIZE] = "";
    char proximity[INTER_BUF_SIZE] = "";
    char torque[INTER_BUF_SIZE] = "";
    struct tm info;
    int result;

    if(fd < 0) {
        return -1;
    }

    if(localtime_r(&stamp.tv_sec, &info) == NULL) {
        return -1;
    }

    if(strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &info) == 0) {
        return -1;
    }

    switch(type) {
        case SENSOR_TYPE_POWCURRVOLT:
            if(flag == 1) {
                result = snprintf(title_buffer, sizeof(title_buffer), "<Timestamp> -> <power>W :: <current>A :: <voltage>V\n");
                if(result < 0 || (size_t)result >= sizeof(title_buffer) || write_all(fd, title_buffer, (size_t)result) == -1) {
                    return -1;
                }
            }

            measurement_to_string(power, sizeof(power), data.powcurrvolt.power, POWER_SCALE);
            measurement_to_string(current, sizeof(current), data.powcurrvolt.current, CURRENT_SCALE);
            measurement_to_string(voltage, sizeof(voltage), data.powcurrvolt.voltage, VOLTAGE_SCALE);
            result = snprintf(write_buffer, sizeof(write_buffer), "%s -> %sW :: %sA :: %sV\n", timestamp, power, current, voltage);
            break;

        case SENSOR_TYPE_TEMPPRESS:
            if(flag == 1) {
                result = snprintf(title_buffer, sizeof(title_buffer), "<Timestamp> -> <temperature>C :: <pressure>Pa\n");
                if(result < 0 || (size_t)result >= sizeof(title_buffer) || write_all(fd, title_buffer, (size_t)result) == -1) {
                    return -1;
                }
            }

            measurement_to_string(temperature, sizeof(temperature), data.temppress.temperature, TEMPERATURE_SCALE);
            measurement_to_string(pressure, sizeof(pressure), (int32_t)data.temppress.pressure, PRESSURE_SCALE);
            result = snprintf(write_buffer, sizeof(write_buffer), "%s -> %sC :: %sPa\n", timestamp, temperature, pressure);
            break;

        case SENSOR_TYPE_PROXIMITY:
            if(flag == 1) {
                result = snprintf(title_buffer, sizeof(title_buffer), "<Timestamp> -> <proximity>m\n");
                if(result < 0 || (size_t)result >= sizeof(title_buffer) || write_all(fd, title_buffer, (size_t)result) == -1) {
                    return -1;
                }
            }

            measurement_to_string(proximity, sizeof(proximity), (int32_t)data.prox.proximity, PROXIMITY_SCALE);
            result = snprintf(write_buffer, sizeof(write_buffer), "%s -> %sm\n", timestamp, proximity);
            break;

        case SENSOR_TYPE_TORQUE:
            if(flag == 1) {
                result = snprintf(title_buffer, sizeof(title_buffer), "<Timestamp> -> <torque>Nm\n");
                if(result < 0 || (size_t)result >= sizeof(title_buffer) || write_all(fd, title_buffer, (size_t)result) == -1) {
                    return -1;
                }
            }

            measurement_to_string(torque, sizeof(torque), data.tor.torque, TORQUE_SCALE);
            result = snprintf(write_buffer, sizeof(write_buffer), "%s -> %sNm\n", timestamp, torque);
            break;

        default:
            return -1;
    }

    if(result < 0 || (size_t)result >= sizeof(write_buffer)) {
        return -1;
    }

    return write_all(fd, write_buffer, (size_t)result);
}

int sensorExists(Sensor_t *frame, Sensor_t *registry, size_t count, int8_t *new_sensor_flag) {
    if(frame == NULL || registry == NULL || new_sensor_flag == NULL) {
        return -1;
    }

    *new_sensor_flag = 0;

    for(size_t idx = 0; idx < count; idx++) {
        if(registry[idx].id == frame->id && registry[idx].type == frame->type) {
            return (int)idx;
        }
    }

    *new_sensor_flag = 1;
    return (int)count;
}

int updateRegistry(Sensor_t *source, Sensor_t *target) {
    if(source == NULL || target == NULL) {
        return -1;
    }

    *target = *source;
    return 0;
}

Sensor_t getSensor_ID(uint16_t id, Sensor_t *registry, size_t count) {
    Sensor_t empty_sensor = {0};

    if(registry == NULL) {
        return empty_sensor;
    }

    for(size_t idx = 0; idx < count; idx++) {
        if(registry[idx].id == id) {
            return registry[idx];
        }
    }

    return empty_sensor;
}

void getTime(Sensor_t frame, char *string) {
    struct tm info;

    if(string == NULL) {
        return;
    }

    if(localtime_r(&frame.timestamp.tv_sec, &info) == NULL) {
        string[0] = '\0';
        return;
    }

    if(strftime(string, INTER_BUF_SIZE, "%Y-%m-%d %H:%M:%S", &info) == 0) {
        string[0] = '\0';
    }
}

void getDataString(Sensor_t frame, char *string) {
    char measurement1[INTER_BUF_SIZE] = "";
    char measurement2[INTER_BUF_SIZE] = "";
    char measurement3[INTER_BUF_SIZE] = "";

    if(string == NULL) {
        return;
    }

    switch(frame.type) {
        case SENSOR_TYPE_POWCURRVOLT:
            measurement_to_string(measurement1, sizeof(measurement1), frame.data.powcurrvolt.power, POWER_SCALE);
            measurement_to_string(measurement2, sizeof(measurement2), frame.data.powcurrvolt.current, CURRENT_SCALE);
            measurement_to_string(measurement3, sizeof(measurement3), frame.data.powcurrvolt.voltage, VOLTAGE_SCALE);
            snprintf(string, WRITE_BUFFER_SIZE, "%sW :: %sA :: %sV", measurement1, measurement2, measurement3);
            break;

        case SENSOR_TYPE_TEMPPRESS:
            measurement_to_string(measurement1, sizeof(measurement1), frame.data.temppress.temperature, TEMPERATURE_SCALE);
            measurement_to_string(measurement2, sizeof(measurement2), (int32_t)frame.data.temppress.pressure, PRESSURE_SCALE);
            snprintf(string, WRITE_BUFFER_SIZE, "%sC :: %sPa", measurement1, measurement2);
            break;

        case SENSOR_TYPE_PROXIMITY:
            measurement_to_string(measurement1, sizeof(measurement1), (int32_t)frame.data.prox.proximity, PROXIMITY_SCALE);
            snprintf(string, WRITE_BUFFER_SIZE, "%sm", measurement1);
            break;

        case SENSOR_TYPE_TORQUE:
            measurement_to_string(measurement1, sizeof(measurement1), frame.data.tor.torque, TORQUE_SCALE);
            snprintf(string, WRITE_BUFFER_SIZE, "%sNm", measurement1);
            break;

        default:
            string[0] = '\0';
            break;
    }
}

SensorType_e findSensorType(uint8_t type) {
    switch(type) {
        case SENSOR_TYPE_TEMPPRESS:
            return SENSOR_TYPE_TEMPPRESS;
        case SENSOR_TYPE_POWCURRVOLT:
            return SENSOR_TYPE_POWCURRVOLT;
        case SENSOR_TYPE_TORQUE:
            return SENSOR_TYPE_TORQUE;
        case SENSOR_TYPE_PROXIMITY:
            return SENSOR_TYPE_PROXIMITY;
        default:
            return SENSOR_TYPE_UNSUPPORTED;
    }
}

static int write_all(int fd, const char *buffer, size_t len) {
    size_t total = 0;

    if(fd < 0 || buffer == NULL) {
        return -1;
    }

    while(total < len) {
        ssize_t bytes_written = write(fd, buffer + total, len - total);

        if(bytes_written < 0) {
            if(errno == EINTR) {
                continue;
            }
            return -1;
        }

        if(bytes_written == 0) {
            return -1;
        }

        total += (size_t)bytes_written;
    }

    return 0;
}

static inline void measurement_to_string(char *buffer, size_t len, int32_t value, int scale) {
    if(buffer == NULL || len == 0 || scale == 0) {
        return;
    }

    snprintf(buffer, len, "%f", (double)value / (double)scale);
}
