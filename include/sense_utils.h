
#ifndef SENSE_UTILS_H
#define SENSE_UTILS_H

#include "sense.h"

SensorType_e findSensorType(uint8_t);
void nameSensor(char*);
int recordSensorData(SensorData_u*, uint8_t*, uint16_t);

#endif
