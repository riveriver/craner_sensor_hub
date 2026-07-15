#ifndef DEVICE_PARAM_STORE_H_
#define DEVICE_PARAM_STORE_H_

#include <stdbool.h>
#include <stddef.h>

enum device_param_type {
	DEVICE_PARAM_TYPE_STRING,
	DEVICE_PARAM_TYPE_INT,
	DEVICE_PARAM_TYPE_ENUM,
};

struct device_param_record {
	const char *key;
	enum device_param_type type;
	const char *value;
	const char *default_value;
	const char *range;
	bool dirty;
	bool loaded_from_settings;
	int last_error;
};

struct device_param_store_status {
	bool initialized;
	bool settings_ready;
	bool dirty;
	size_t param_count;
	int load_count;
	int save_count;
	int fail_count;
	int last_error;
};

int device_param_store_init(void);
void device_param_store_get_status(struct device_param_store_status *status);
size_t device_param_store_count(void);
const struct device_param_record *device_param_store_get_by_index(size_t index);
const struct device_param_record *device_param_store_find(const char *key);
int device_param_store_get(const char *key, char *buf, size_t len);
int device_param_store_set(const char *key, const char *value);
int device_param_store_save(void);
int device_param_store_factory_reset(void);
int device_param_store_format_status(char *buf, size_t len);
int device_param_store_format_all(char *buf, size_t len);

#endif /* DEVICE_PARAM_STORE_H_ */
