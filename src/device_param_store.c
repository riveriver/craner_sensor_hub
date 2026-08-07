#include "device_param_store.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(device_param_store, CONFIG_LOG_DEFAULT_LEVEL);

#define PARAM_SETTINGS_PREFIX "param"
#define PARAM_KEY_MAX_LEN 64
#define PARAM_VALUE_MAX_LEN 96
#define PARAM_SETTINGS_NAME_MAX_LEN (sizeof(PARAM_SETTINGS_PREFIX) + PARAM_KEY_MAX_LEN + 1U)

struct device_param_entry {
	struct device_param_record record;
	char value[PARAM_VALUE_MAX_LEN];
	const char *enum_values;
	int min_value;
	int max_value;
	bool verify_required;
};

static struct device_param_entry params[] = {
	{
		.record = {
			.key = "time/sync_mode",
			.type = DEVICE_PARAM_TYPE_ENUM,
			.default_value = "auto",
			.range = "auto|manual",
		},
		.enum_values = "auto|manual",
	},
	{
		.record = {
			.key = "time/ntp_server",
			.type = DEVICE_PARAM_TYPE_STRING,
			.default_value = CONFIG_CRANER_TIME_SERVICE_NTP_SERVER,
			.range = "1..63 printable chars",
		},
	},
	{
		.record = {
			.key = "device/project",
			.type = DEVICE_PARAM_TYPE_STRING,
			.default_value = "project",
			.range = "1..23 hostname chars",
		},
	},
	{
		.record = {
			.key = "device/type",
			.type = DEVICE_PARAM_TYPE_STRING,
			.default_value = "type",
			.range = "1..15 hostname chars",
		},
	},
	{
		.record = {
			.key = "shell/output_format",
			.type = DEVICE_PARAM_TYPE_ENUM,
			.default_value = "kv",
			.range = "kv|json",
		},
		.enum_values = "kv|json",
	},
};

static struct device_param_store_status store_status = {
	.param_count = ARRAY_SIZE(params),
};

static K_MUTEX_DEFINE(param_lock);

static const char *type_name(enum device_param_type type)
{
	switch (type) {
	case DEVICE_PARAM_TYPE_STRING:
		return "string";
	case DEVICE_PARAM_TYPE_INT:
		return "int";
	case DEVICE_PARAM_TYPE_ENUM:
		return "enum";
	default:
		return "unknown";
	}
}

static bool is_printable_ascii_string(const char *value, bool allow_empty)
{
	size_t len;

	if (value == NULL) {
		return false;
	}

	len = strlen(value);
	if (!allow_empty && len == 0U) {
		return false;
	}

	if (len >= PARAM_VALUE_MAX_LEN) {
		return false;
	}

	for (size_t i = 0U; i < len; i++) {
		unsigned char ch = (unsigned char)value[i];

		if (ch < 0x20U || ch > 0x7eU || ch == '"' || ch == '\\') {
			return false;
		}
	}

	return true;
}

static bool is_hostname_string(const char *value)
{
	size_t len;

	if (value == NULL) {
		return false;
	}

	len = strlen(value);
	if (len == 0U || len >= PARAM_VALUE_MAX_LEN) {
		return false;
	}

	for (size_t i = 0U; i < len; i++) {
		char ch = value[i];

		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
		    (ch >= '0' && ch <= '9') || ch == '-') {
			continue;
		}

		return false;
	}

	return true;
}

static bool enum_value_allowed(const char *allowed, const char *value)
{
	const char *start = allowed;
	size_t value_len;

	if (allowed == NULL || value == NULL) {
		return false;
	}

	value_len = strlen(value);
	while (*start != '\0') {
		const char *end = strchr(start, '|');
		size_t token_len = end != NULL ? (size_t)(end - start) :
						 strlen(start);

		if (token_len == value_len && strncmp(start, value, value_len) == 0) {
			return true;
		}

		if (end == NULL) {
			break;
		}
		start = end + 1;
	}

	return false;
}

static int validate_param_value(const struct device_param_entry *entry,
				const char *value)
{
	char *end;
	long parsed;

	if (entry == NULL || value == NULL) {
		return -EINVAL;
	}

	switch (entry->record.type) {
	case DEVICE_PARAM_TYPE_STRING:
		if (strcmp(entry->record.key, "device/project") == 0) {
			if (!is_hostname_string(value)) {
				return -EINVAL;
			}
			return strlen(value) <= 23U ? 0 : -ERANGE;
		}
		if (strcmp(entry->record.key, "device/type") == 0) {
			if (!is_hostname_string(value)) {
				return -EINVAL;
			}
			return strlen(value) <= 15U ? 0 : -ERANGE;
		}
		return is_printable_ascii_string(value, false) ? 0 : -EINVAL;

	case DEVICE_PARAM_TYPE_INT:
		errno = 0;
		parsed = strtol(value, &end, 10);
		if (errno != 0 || end == value || *end != '\0') {
			return -EINVAL;
		}
		if (parsed < entry->min_value || parsed > entry->max_value) {
			return -ERANGE;
		}
		return 0;

	case DEVICE_PARAM_TYPE_ENUM:
		return enum_value_allowed(entry->enum_values, value) ? 0 : -EINVAL;

	default:
		return -EINVAL;
	}
}

static void set_default_values(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(params); i++) {
		snprintk(params[i].value, sizeof(params[i].value), "%s",
			 params[i].record.default_value);
		params[i].record.value = params[i].value;
		params[i].record.dirty = false;
		params[i].record.loaded_from_settings = false;
		params[i].record.last_error = 0;
	}
}

static struct device_param_entry *find_entry(const char *key)
{
	if (key == NULL) {
		return NULL;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(params); i++) {
		if (strcmp(params[i].record.key, key) == 0) {
			return &params[i];
		}
	}

	return NULL;
}

static int make_settings_name(const char *key, char *buf, size_t len)
{
	int written;

	if (key == NULL || buf == NULL || len == 0U) {
		return -EINVAL;
	}

	written = snprintk(buf, len, "%s/%s", PARAM_SETTINGS_PREFIX, key);
	if (written < 0) {
		return written;
	}

	if ((size_t)written >= len) {
		return -EMSGSIZE;
	}

	return 0;
}

static bool any_dirty_unlocked(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(params); i++) {
		if (params[i].record.dirty) {
			return true;
		}
	}

	return false;
}

static int verify_entry_saved(struct device_param_entry *entry)
{
	char settings_name[PARAM_SETTINGS_NAME_MAX_LEN];
	char value[PARAM_VALUE_MAX_LEN];
	ssize_t len;
	int rc;

	rc = make_settings_name(entry->record.key, settings_name,
				sizeof(settings_name));
	if (rc != 0) {
		return rc;
	}

	len = settings_load_one(settings_name, value, sizeof(value));
	if (len < 0) {
		return (int)len;
	}

	if (len == 0) {
		return -ENODATA;
	}

	if ((size_t)len >= sizeof(value)) {
		return -EMSGSIZE;
	}

	value[len] = '\0';
	return strcmp(value, entry->value) == 0 ? 0 : -EBADMSG;
}

static int load_one_param(struct device_param_entry *entry)
{
	char settings_name[PARAM_SETTINGS_NAME_MAX_LEN];
	char value[PARAM_VALUE_MAX_LEN];
	ssize_t len;
	int rc;

	rc = make_settings_name(entry->record.key, settings_name,
				sizeof(settings_name));
	if (rc != 0) {
		entry->record.last_error = rc;
		return rc;
	}

	len = settings_load_one(settings_name, value, sizeof(value));
	if (len < 0) {
		entry->record.last_error = (int)len;
		return (int)len;
	}

	if (len == 0) {
		return 0;
	}

	if ((size_t)len >= sizeof(value)) {
		entry->record.last_error = -EMSGSIZE;
		return -EMSGSIZE;
	}

	value[len] = '\0';
	rc = validate_param_value(entry, value);
	if (rc != 0) {
		entry->record.last_error = rc;
		return rc;
	}

	snprintk(entry->value, sizeof(entry->value), "%s", value);
	entry->record.value = entry->value;
	entry->record.loaded_from_settings = true;
	entry->record.last_error = 0;

	return 0;
}

int device_param_store_init(void)
{
	uint32_t loaded_count = 0U;
	uint32_t rejected_count = 0U;
	int rc;

	k_mutex_lock(&param_lock, K_FOREVER);
	set_default_values();
	store_status.param_count = ARRAY_SIZE(params);
	k_mutex_unlock(&param_lock);

	rc = settings_subsys_init();
	if (rc != 0 && rc != -EALREADY) {
		store_status.initialized = true;
		store_status.settings_ready = false;
		store_status.last_error = rc;
		store_status.fail_count++;
		LOG_ERR("Settings subsystem init failed: %d", rc);
		return rc;
	}

	k_mutex_lock(&param_lock, K_FOREVER);
	store_status.settings_ready = true;
	store_status.load_count = 0;
	store_status.last_error = 0;

	for (size_t i = 0U; i < ARRAY_SIZE(params); i++) {
		rc = load_one_param(&params[i]);
		if (rc != 0) {
			rejected_count++;
			store_status.fail_count++;
			store_status.last_error = rc;
		} else if (params[i].record.loaded_from_settings) {
			loaded_count++;
		}
	}

	store_status.load_count = loaded_count;
	store_status.dirty = any_dirty_unlocked();
	store_status.initialized = true;
	k_mutex_unlock(&param_lock);

	if (rejected_count > 0U) {
		LOG_WRN("Device parameter store loaded defaults for %u rejected setting(s), last_error=%d",
			rejected_count, store_status.last_error);
	}
	LOG_INF("Device parameter store ready: loaded=%d, params=%u",
		store_status.load_count, (uint32_t)ARRAY_SIZE(params));

	return 0;
}

void device_param_store_get_status(struct device_param_store_status *status)
{
	if (status == NULL) {
		return;
	}

	k_mutex_lock(&param_lock, K_FOREVER);
	*status = store_status;
	status->dirty = any_dirty_unlocked();
	k_mutex_unlock(&param_lock);
}

size_t device_param_store_count(void)
{
	return ARRAY_SIZE(params);
}

const struct device_param_record *device_param_store_get_by_index(size_t index)
{
	if (index >= ARRAY_SIZE(params)) {
		return NULL;
	}

	return &params[index].record;
}

const struct device_param_record *device_param_store_find(const char *key)
{
	struct device_param_entry *entry = find_entry(key);

	return entry != NULL ? &entry->record : NULL;
}

int device_param_store_get(const char *key, char *buf, size_t len)
{
	struct device_param_entry *entry;
	int written;

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&param_lock, K_FOREVER);
	entry = find_entry(key);
	if (entry == NULL) {
		k_mutex_unlock(&param_lock);
		return -ENOENT;
	}

	written = snprintk(buf, len, "%s", entry->value);
	k_mutex_unlock(&param_lock);

	if (written < 0) {
		return written;
	}

	return (size_t)written >= len ? -EMSGSIZE : 0;
}

int device_param_store_set(const char *key, const char *value)
{
	struct device_param_entry *entry;
	int rc;

	k_mutex_lock(&param_lock, K_FOREVER);
	entry = find_entry(key);
	if (entry == NULL) {
		k_mutex_unlock(&param_lock);
		return -ENOENT;
	}

	rc = validate_param_value(entry, value);
	if (rc != 0) {
		entry->record.last_error = rc;
		store_status.last_error = rc;
		store_status.fail_count++;
		k_mutex_unlock(&param_lock);
		return rc;
	}

	snprintk(entry->value, sizeof(entry->value), "%s", value);
	entry->record.value = entry->value;
	entry->record.dirty = true;
	entry->record.last_error = 0;
	store_status.dirty = true;
	k_mutex_unlock(&param_lock);

	return 0;
}

int device_param_store_save(void)
{
	char settings_name[PARAM_SETTINGS_NAME_MAX_LEN];
	uint32_t save_required_count = 0U;
	int rc;

	k_mutex_lock(&param_lock, K_FOREVER);

	for (size_t i = 0U; i < ARRAY_SIZE(params); i++) {
		params[i].verify_required = params[i].record.dirty;
		if (params[i].verify_required) {
			save_required_count++;
		}
	}

	if (save_required_count == 0U) {
		store_status.dirty = false;
		store_status.last_error = 0;
		k_mutex_unlock(&param_lock);
		return 0;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(params); i++) {
		if (!params[i].verify_required) {
			continue;
		}

		rc = make_settings_name(params[i].record.key, settings_name,
					sizeof(settings_name));
		if (rc != 0) {
			store_status.last_error = rc;
			store_status.fail_count++;
			k_mutex_unlock(&param_lock);
			return rc;
		}

		rc = settings_save_one(settings_name, params[i].value,
				       strlen(params[i].value) + 1U);
		if (rc != 0) {
			params[i].record.last_error = rc;
			store_status.last_error = rc;
			store_status.fail_count++;
			k_mutex_unlock(&param_lock);
			return rc;
		}

		params[i].record.loaded_from_settings = true;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(params); i++) {
		if (!params[i].verify_required) {
			continue;
		}

		rc = verify_entry_saved(&params[i]);
		if (rc != 0) {
			params[i].record.last_error = rc;
			store_status.last_error = rc;
			store_status.fail_count++;
			k_mutex_unlock(&param_lock);
			return rc;
		}
	}

	for (size_t i = 0U; i < ARRAY_SIZE(params); i++) {
		if (params[i].verify_required) {
			params[i].record.dirty = false;
			params[i].verify_required = false;
		}
	}

	store_status.save_count++;
	store_status.dirty = any_dirty_unlocked();
	store_status.last_error = 0;
	k_mutex_unlock(&param_lock);

	return 0;
}

int device_param_store_factory_reset(void)
{
	char settings_name[PARAM_SETTINGS_NAME_MAX_LEN];
	int rc;
	int first_error = 0;

	k_mutex_lock(&param_lock, K_FOREVER);

	for (size_t i = 0U; i < ARRAY_SIZE(params); i++) {
		rc = make_settings_name(params[i].record.key, settings_name,
					sizeof(settings_name));
		if (rc == 0) {
			rc = settings_delete(settings_name);
			if (rc == -ENOENT) {
				rc = 0;
			}
		}

		if (rc != 0 && first_error == 0) {
			first_error = rc;
		}
	}

	set_default_values();
	store_status.dirty = false;
	store_status.last_error = first_error;
	if (first_error != 0) {
		store_status.fail_count++;
	}
	k_mutex_unlock(&param_lock);

	return first_error;
}

int device_param_store_format_status(char *buf, size_t len)
{
	struct device_param_store_status status;
	int written;

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	device_param_store_get_status(&status);

	written = snprintk(buf, len,
			   "{\"type\":\"params\",\"initialized\":%s,"
			   "\"settings_ready\":%s,\"dirty\":%s,"
			   "\"param_count\":%u,\"load_count\":%d,"
			   "\"save_count\":%d,\"fail_count\":%d,"
			   "\"last_error\":%d}",
			   status.initialized ? "true" : "false",
			   status.settings_ready ? "true" : "false",
			   status.dirty ? "true" : "false",
			   (uint32_t)status.param_count, status.load_count,
			   status.save_count, status.fail_count,
			   status.last_error);
	if (written < 0) {
		return written;
	}

	return (size_t)written >= len ? -EMSGSIZE : 0;
}

int device_param_store_format_all(char *buf, size_t len)
{
	size_t used = 0U;
	int written;

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&param_lock, K_FOREVER);

	written = snprintk(buf, len, "{\"type\":\"params\",\"items\":[");
	if (written < 0 || (size_t)written >= len) {
		k_mutex_unlock(&param_lock);
		return written < 0 ? written : -EMSGSIZE;
	}
	used = (size_t)written;

	for (size_t i = 0U; i < ARRAY_SIZE(params); i++) {
		written = snprintk(&buf[used], len - used,
				   "%s{\"key\":\"%s\",\"type\":\"%s\","
				   "\"value\":\"%s\",\"default\":\"%s\","
				   "\"range\":\"%s\",\"dirty\":%s,"
				   "\"loaded\":%s,\"last_error\":%d}",
				   i == 0U ? "" : ",",
				   params[i].record.key,
				   type_name(params[i].record.type),
				   params[i].value,
				   params[i].record.default_value,
				   params[i].record.range,
				   params[i].record.dirty ? "true" : "false",
				   params[i].record.loaded_from_settings ? "true" : "false",
				   params[i].record.last_error);
		if (written < 0 || (size_t)written >= len - used) {
			k_mutex_unlock(&param_lock);
			return written < 0 ? written : -EMSGSIZE;
		}
		used += (size_t)written;
	}

	written = snprintk(&buf[used], len - used, "]}");
	k_mutex_unlock(&param_lock);

	if (written < 0) {
		return written;
	}

	return (size_t)written >= len - used ? -EMSGSIZE : 0;
}
