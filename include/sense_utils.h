
#ifndef SENSE_UTILS_H
#define SENSE_UTILS_H

#include "sense.h"

// processing_d functions
void nameSensor(char*);
int recordSensorData(SensorData_u*, uint8_t*, uint16_t);

// logging_d function
int writeFrameData(SensorData_u, sensor_timespec_t, uint8_t, int);

// telemetry_d functions
int sensorExists(Sensor_t*, Sensor_t*, size_t, int8_t*);
int updateRegistry(Sensor_t*, Sensor_t*);
Sensor_t getSensor_ID(uint16_t, Sensor_t*);
void getTime(Sensor_t, char*);

// telemetry_d && processing_d functions
SensorType_e findSensorType(uint8_t);

#endif
