
#ifndef SENSE_TYPE_H
#define SENSE_TYPE_H

#include <stdint.h>
#include <time.h>

#define SENSORS_LIMIT_MAX 128
#define SENSOR_NAME_SIZE 9
#define SENSOR_INACTIVE_SECS_MAX 30

// First 'N' Digits of Sensor Name
#define SENSOR_NAME_3_TEMPRESS "TPS"
#define SENSOR_NAME_4_POWCURRVOLT "PCVS"
#define SENSOR_NAME_5_TORQUE "TORQS"
#define SENSOR_NAME_5_PROXIMITY "PROXS"
/*
Sensor Name Structure: 
"SENSOR_NAME_<N>_<SENSORTYPE>" "<SENSORTYPENUMBER>" "<SENSORID>"
*/

typedef struct timespec sensor_timespec_t;

typedef enum ScaleFactor {
    TEMP_SCALE = 10,
    PRESSURE_SCALE = 100,
    VOLTAGE_SCALE = 100,
    CURRENT_SCALE = 100,
    POWER_SCALE = 10,
    TORQUE_SCALE = 10,
    PROXIMITY_SCALE = 10
} ScaleFactor_e;

typedef enum SensorType {
    SENSOR_TYPE_TEMPPRESS = 0x1B,
    SENSOR_TYPE_POWCURRVOLT = 0x2B,
    SENSOR_TYPE_TORQUE = 0x4B,
    SENSOR_TYPE_PROXIMITY = 0x8B,
    SENSOR_TYPE_UNSUPPORTED
} SensorType_e;

typedef enum SensorState {
    SENSOR_FRAME_DEFAULT,
    SENSOR_OFFLINE,
    SENSOR_ONLINE
} SensorState_e;

typedef enum SensorFrameType {
    SENSOR_HEARTBEAT,
    SENSOR_DATA
} SensorFrameType_e;

typedef struct TempPress {
    int32_t temperature;
    uint32_t pressure;
} TempPress_t;

typedef struct PowCurrVolt {
    int32_t power;
    int32_t current;
    int32_t volt;
} PowCurrVolt_t;

typedef struct Torque {
    int32_t torque;
} Torque_t;

typedef struct Proximity {
    uint32_t proximity;
} Proximity_t;

typedef union SensorData {
    PowCurrVolt_t powcurrvolt;
    TempPress_t temppress;
    Torque_t tor;
    Proximity_t prox;
} SensorData_u;

typedef struct Sensor {
    SensorData_u data;
    sensor_timespec_t timestamp;
    sensor_timespec_t last_seen_time;
    uint16_t id;
    char name[SENSOR_NAME_SIZE];
    SensorType_e type;
    SensorState_e state;
    SensorFrameType_e frame_type;
} Sensor_t;

#endif
