
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

static inline void measurement_to_string(char*, size_t, int32_t, int);
static int write_all(int, const char*, size_t);

void nameSensor(char *string, uint16_t id, SensorType_e type) {
    size_t string_size = SENSOR_NAME_SIZE;

    const char *name_base;
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

    snprintf(string, string_size, "%s_0X%" PRIx16, name_base, id);
}

int recordSensorData(SensorData_u *target, uint8_t *raw_data, uint16_t byte_size, SensorType_e type) {
    uint16_t idx = 0;
    uint32_t value;

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
    size_t write_len = 0;

    struct tm info;
    if(localtime_r(&stamp.tv_sec, &info) == NULL) {
        return -1;
    }

    char timestamp[INTER_BUF_SIZE] = "";
    if(strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &info) == 0) {
        return -1;
    }

    switch(type) {
        case SENSOR_TYPE_POWCURRVOLT:
            if(flag == 1) {
                snprintf(title_buffer, sizeof(title_buffer), "<Timestamp> -> <power>W :: <current>A :: <voltage>V\n");
                write_len = strlen(title_buffer);
                if(write_all(fd, title_buffer, write_len) == -1) {
                    return -1;
                }
            }

            char power[INTER_BUF_SIZE], current[INTER_BUF_SIZE], voltage[INTER_BUF_SIZE];
            measurement_to_string(power, INTER_BUF_SIZE, data.powcurrvolt.power, POWER_SCALE);
            measurement_to_string(current, INTER_BUF_SIZE, data.powcurrvolt.current, CURRENT_SCALE);
            measurement_to_string(voltage, INTER_BUF_SIZE, data.powcurrvolt.voltage, VOLTAGE_SCALE);
            snprintf(write_buffer, sizeof(write_buffer), "%s -> %sW :: %sA :: %sV\n", timestamp, power, current, voltage);

            write_len = strlen(write_buffer);
            if(write_all(fd, write_buffer, write_len) == -1) {
                return -1;
            }
            break;

        case SENSOR_TYPE_TEMPPRESS:
            if(flag == 1) {
                snprintf(title_buffer, sizeof(title_buffer), "<Timestamp> -> <temperature>C :: <pressure>Pa\n");
                write_len = strlen(title_buffer);
                if(write_all(fd, title_buffer, write_len) == -1) {
                    return -1;
                }
            }

            char temperature[INTER_BUF_SIZE], pressure[INTER_BUF_SIZE];
            measurement_to_string(temperature, INTER_BUF_SIZE, data.temppress.temperature, TEMPERATURE_SCALE);
            measurement_to_string(pressure, INTER_BUF_SIZE, data.temppress.pressure, PRESSURE_SCALE);
            snprintf(write_buffer, sizeof(write_buffer), "%s -> %sC :: %sPa\n", timestamp, temperature, pressure);

            write_len = strlen(write_buffer);
            if(write_all(fd, write_buffer, write_len) == -1) {
                return -1;
            }
            break;

        case SENSOR_TYPE_PROXIMITY:
            if(flag == 1) {
                snprintf(title_buffer, sizeof(title_buffer), "<Timestamp> -> <proximity>m\n");
                write_len = strlen(title_buffer);
                if(write_all(fd, title_buffer, write_len) == -1) {
                    return -1;
                }
            }

            char proximity[INTER_BUF_SIZE];
            measurement_to_string(proximity, INTER_BUF_SIZE, data.prox.proximity, PROXIMITY_SCALE);
            snprintf(write_buffer, sizeof(write_buffer), "%s -> %sm\n", timestamp, proximity);

            write_len = strlen(write_buffer);
            if(write_all(fd, write_buffer, write_len) == -1) {
                return -1;
            }
            break;

        case SENSOR_TYPE_TORQUE:
            if(flag == 1) {
                snprintf(title_buffer, sizeof(title_buffer), "<Timestamp> -> <torque>Nm\n");
                write_len = strlen(title_buffer);
                if(write_all(fd, title_buffer, write_len) == -1) {
                    return -1;
                }
            }

            char torque[INTER_BUF_SIZE];
            measurement_to_string(torque, INTER_BUF_SIZE, data.tor.torque, TORQUE_SCALE);
            snprintf(write_buffer, sizeof(write_buffer), "%s -> %sNm\n", timestamp, torque);

            write_len = strlen(write_buffer);
            if(write_all(fd, write_buffer, write_len) == -1) {
                return -1;
            }
            break;

        default:
            return -1;
    }

    return 0;
}

int sensorExists(Sensor_t *frame, Sensor_t *registry, size_t count, int8_t *new_sensor_flag) {
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

    for(size_t idx = 0; idx < count; idx++) {
        if(registry[idx].id == id) {
            return registry[idx];
        }
    }

    return empty_sensor;
}

void getTime(Sensor_t frame, char *string) {
    struct tm info;

    if(localtime_r(&frame.timestamp.tv_sec, &info) == NULL) {
        string[0] = '\0';
        return;
    }

    if(strftime(string, INTER_BUF_SIZE, "%Y-%m-%d %H:%M:%S", &info) == 0) {
        string[0] = '\0';
    }
}

void getDataString(Sensor_t frame, char *string) {
    char measurement[INTER_BUF_SIZE];

    switch(frame.type) {
        case SENSOR_TYPE_POWCURRVOLT:
            measurement_to_string(measurement, sizeof(measurement), frame.data.powcurrvolt.power, POWER_SCALE);
            snprintf(string, WRITE_BUFFER_SIZE, "%sW :: ", measurement);
            measurement_to_string(measurement, sizeof(measurement), frame.data.powcurrvolt.current, CURRENT_SCALE);
            strncat(string, measurement, WRITE_BUFFER_SIZE - strlen(string) - 1);
            strncat(string, "A :: ", WRITE_BUFFER_SIZE - strlen(string) - 1);
            measurement_to_string(measurement, sizeof(measurement), frame.data.powcurrvolt.voltage, VOLTAGE_SCALE);
            strncat(string, measurement, WRITE_BUFFER_SIZE - strlen(string) - 1);
            strncat(string, "V", WRITE_BUFFER_SIZE - strlen(string) - 1);
            break;

        case SENSOR_TYPE_TEMPPRESS:
            measurement_to_string(measurement, sizeof(measurement), frame.data.temppress.temperature, TEMPERATURE_SCALE);
            snprintf(string, WRITE_BUFFER_SIZE, "%sC :: ", measurement);
            measurement_to_string(measurement, sizeof(measurement), frame.data.temppress.pressure, PRESSURE_SCALE);
            strncat(string, measurement, WRITE_BUFFER_SIZE - strlen(string) - 1);
            strncat(string, "Pa", WRITE_BUFFER_SIZE - strlen(string) - 1);
            break;

        case SENSOR_TYPE_PROXIMITY:
            measurement_to_string(measurement, sizeof(measurement), frame.data.prox.proximity, PROXIMITY_SCALE);
            snprintf(string, WRITE_BUFFER_SIZE, "%sm", measurement);
            break;

        case SENSOR_TYPE_TORQUE:
            measurement_to_string(measurement, sizeof(measurement), frame.data.tor.torque, TORQUE_SCALE);
            snprintf(string, WRITE_BUFFER_SIZE, "%sNm", measurement);
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
    float actual_val = value/(float)scale;
    snprintf(buffer, len, "%f", actual_val);
}
