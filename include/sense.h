
#ifndef SENSE_TYPE_H
#define SENSE_TYPE_H

#include <stdint.h>

#define SENSORS_LIMIT_MAX 128
#define SENSOR_NAME_SIZE 9

// First 'N' Digits of Sensor Name
#define SENSOR_NAME_3_TEMPRESS "TPS"
#define SENSOR_NAME_4_POWCURRVOLT "PCVS"
#define SENSOR_NAME_5_TORQUE "TORQS"
#define SENSOR_NAME_5_PROXIMITY "PROXS"
/*
Sensor Name Structure: 
"SENSOR_NAME_<N>_<SENSORTYPE>" "<SENSORTYPENUMBER>" "<SENSORID>"
*/

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
    SENSOR_TYPE_TEMPPRESS,
    SENSOR_TYPE_POWCURRVOLT,
    SENSOR_TYPE_TORQUE,
    SENSOR_TYPE_PROXIMITY
} SensorType_e;

typedef enum SensorState {
    SENSOR_ONLINE,
    SENSOR_OFFLINE
} SensorState_e;

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
    uint64_t timestamp;
    uint16_t id;
    uint8_t scale_factor;
    char name[SENSOR_NAME_SIZE];
    SensorType_e type;
    SensorState_e state;
} Sensor_t;

#endif
