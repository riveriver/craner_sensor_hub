#ifndef SENSOR_MANAGE_APP_H_
#define SENSOR_MANAGE_APP_H_

#include <stdbool.h>

enum sensor_manage_id {
	SENSOR_MANAGE_SLEWING,
	SENSOR_MANAGE_LUFFING,
	SENSOR_MANAGE_HOISTING,
	SENSOR_MANAGE_ANEMOMETER,
	SENSOR_MANAGE_LOAD_ADC,
};

bool sensor_manage_is_enabled(enum sensor_manage_id id);

#endif
