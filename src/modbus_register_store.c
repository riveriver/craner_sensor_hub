#include "modbus_register_store.h"

#include "modbus_register_service.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>

LOG_MODULE_REGISTER(modbus_register_store, CONFIG_LOG_DEFAULT_LEVEL);

#define MODBUS_STORE_PARTITION_NODE DT_NODELABEL(modbus_store_partition)
BUILD_ASSERT(DT_NODE_EXISTS(MODBUS_STORE_PARTITION_NODE),
	     "Missing modbus_store_partition fixed partition");

#define MODBUS_STORE_AREA_ID DT_FIXED_PARTITION_ID(MODBUS_STORE_PARTITION_NODE)

#define MODBUS_STORE_MAGIC 0x4d425354U /* MBST */
#define MODBUS_STORE_VERSION 1U
#define MODBUS_STORE_BANK_COUNT 2U
#define MODBUS_STORE_RECORD_COIL_BOOL 1U
#define MODBUS_STORE_RECORD_HOLDING_U16 2U

enum modbus_store_stage {
	MODBUS_STORE_STAGE_NONE = 0,
	MODBUS_STORE_STAGE_OPEN = 1,
	MODBUS_STORE_STAGE_INIT_CHECK = 2,
	MODBUS_STORE_STAGE_LOAD_SELECT = 3,
	MODBUS_STORE_STAGE_LOAD_APPLY = 4,
	MODBUS_STORE_STAGE_BUILD_PAYLOAD = 5,
	MODBUS_STORE_STAGE_SELECT_BANK = 6,
	MODBUS_STORE_STAGE_SIZE_CHECK = 7,
	MODBUS_STORE_STAGE_ERASE = 8,
	MODBUS_STORE_STAGE_WRITE_PAYLOAD = 9,
	MODBUS_STORE_STAGE_WRITE_HEADER = 10,
	MODBUS_STORE_STAGE_VALIDATE = 11,
	MODBUS_STORE_STAGE_CLEAR = 12,
};

struct modbus_store_header {
	uint32_t magic;
	uint16_t version;
	uint16_t header_size;
	uint32_t sequence;
	uint32_t payload_size;
	uint32_t payload_crc32;
	uint32_t header_crc32;
};

struct modbus_store_record {
	uint8_t type;
	uint8_t reserved;
	uint16_t address;
	uint16_t value;
	uint16_t value_len;
};

struct payload_builder {
	uint8_t *buf;
	size_t len;
	size_t used;
};

static struct modbus_register_store_status store_status;
static const struct flash_area *store_area;
static size_t bank_size;
static K_MUTEX_DEFINE(store_lock);
static void save_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(save_work, save_work_handler);

static void set_failure(int stage, int rc)
{
	store_status.fail_count++;
	store_status.last_stage = stage;
	store_status.last_error = rc;
}

static uint32_t header_crc(const struct modbus_store_header *header)
{
	return crc32_ieee((const uint8_t *)header,
			  offsetof(struct modbus_store_header, header_crc32));
}

static off_t bank_offset(uint8_t bank)
{
	return (off_t)((size_t)bank * bank_size);
}

static int read_header(uint8_t bank, struct modbus_store_header *header)
{
	int rc;

	rc = flash_area_read(store_area, bank_offset(bank), header,
			     sizeof(*header));
	if (rc != 0) {
		return rc;
	}

	if (header->magic != MODBUS_STORE_MAGIC ||
	    header->version != MODBUS_STORE_VERSION ||
	    header->header_size != sizeof(*header)) {
		return -EPROTO;
	}

	if (header->payload_size > bank_size - sizeof(*header)) {
		return -EMSGSIZE;
	}

	if (header->header_crc32 != header_crc(header)) {
		return -EBADMSG;
	}

	return 0;
}

static int validate_bank(uint8_t bank, struct modbus_store_header *header)
{
	uint8_t chunk[128];
	uint32_t crc = 0U;
	size_t remaining;
	off_t offset;
	int rc;

	rc = read_header(bank, header);
	if (rc != 0) {
		return rc;
	}

	remaining = header->payload_size;
	offset = bank_offset(bank) + sizeof(*header);
	while (remaining > 0U) {
		size_t chunk_len = MIN(remaining, sizeof(chunk));

		rc = flash_area_read(store_area, offset, chunk, chunk_len);
		if (rc != 0) {
			return rc;
		}

		crc = crc32_ieee_update(crc, chunk, chunk_len);
		remaining -= chunk_len;
		offset += (off_t)chunk_len;
	}

	return crc == header->payload_crc32 ? 0 : -EBADMSG;
}

static int select_active_bank(uint8_t *bank,
			      struct modbus_store_header *selected_header)
{
	struct modbus_store_header headers[MODBUS_STORE_BANK_COUNT];
	bool valid[MODBUS_STORE_BANK_COUNT] = { false };

	for (uint8_t i = 0U; i < MODBUS_STORE_BANK_COUNT; i++) {
		valid[i] = validate_bank(i, &headers[i]) == 0;
	}

	if (!valid[0] && !valid[1]) {
		return -ENOENT;
	}

	if (valid[0] && (!valid[1] ||
			 headers[0].sequence >= headers[1].sequence)) {
		*bank = 0U;
		*selected_header = headers[0];
		return 0;
	}

	*bank = 1U;
	*selected_header = headers[1];
	return 0;
}

static int append_record(struct payload_builder *builder, uint8_t type,
			 uint16_t addr, uint16_t value)
{
	struct modbus_store_record record = {
		.type = type,
		.address = addr,
		.value = value,
		.value_len = sizeof(value),
	};

	if (builder->used + sizeof(record) > builder->len) {
		return -ENOSPC;
	}

	memcpy(&builder->buf[builder->used], &record, sizeof(record));
	builder->used += sizeof(record);

	return 0;
}

static int append_coil_record(uint16_t addr, bool value, void *user_data)
{
	return append_record(user_data, MODBUS_STORE_RECORD_COIL_BOOL, addr,
			     value ? 1U : 0U);
}

static int append_holding_record(uint16_t addr, uint16_t value,
				 void *user_data)
{
	return append_record(user_data, MODBUS_STORE_RECORD_HOLDING_U16, addr,
			     value);
}

static int build_payload(uint8_t *payload, size_t payload_len, size_t *used)
{
	struct payload_builder builder = {
		.buf = payload,
		.len = payload_len,
	};
	int rc;

	rc = modbus_register_service_foreach_persistent(append_coil_record,
							append_holding_record,
							&builder);
	if (rc != 0) {
		return rc;
	}

	*used = builder.used;
	return 0;
}

static int apply_payload(uint8_t bank,
			 const struct modbus_store_header *header)
{
	struct modbus_store_record record;
	size_t remaining = header->payload_size;
	off_t offset = bank_offset(bank) + sizeof(*header);
	int rc;

	while (remaining >= sizeof(record)) {
		rc = flash_area_read(store_area, offset, &record, sizeof(record));
		if (rc != 0) {
			return rc;
		}

		if (record.value_len != sizeof(record.value)) {
			return -EINVAL;
		}

		switch (record.type) {
		case MODBUS_STORE_RECORD_COIL_BOOL:
			(void)modbus_register_service_restore_persistent_coil(
				record.address, record.value != 0U);
			break;
		case MODBUS_STORE_RECORD_HOLDING_U16:
			(void)modbus_register_service_restore_persistent_holding(
				record.address, record.value);
			break;
		default:
			break;
		}

		remaining -= sizeof(record);
		offset += sizeof(record);
	}

	return remaining == 0U ? 0 : -EINVAL;
}

int modbus_register_store_load(void)
{
	struct modbus_store_header header;
	uint8_t bank;
	int rc;

	k_mutex_lock(&store_lock, K_FOREVER);

	if (store_area == NULL) {
		store_status.last_stage = MODBUS_STORE_STAGE_LOAD_SELECT;
		store_status.last_error = -ENODEV;
		k_mutex_unlock(&store_lock);
		return -ENODEV;
	}

	store_status.last_stage = MODBUS_STORE_STAGE_LOAD_SELECT;
	rc = select_active_bank(&bank, &header);
	if (rc == -ENOENT) {
		store_status.active_bank_valid = false;
		store_status.last_error = 0;
		k_mutex_unlock(&store_lock);
		return 0;
	}

	if (rc == 0) {
		store_status.last_stage = MODBUS_STORE_STAGE_LOAD_APPLY;
		rc = apply_payload(bank, &header);
	}

	if (rc == 0) {
		store_status.active_bank_valid = true;
		store_status.active_bank = bank;
		store_status.active_sequence = header.sequence;
		store_status.payload_size = header.payload_size;
		store_status.load_count++;
		store_status.last_error = 0;
	} else {
		set_failure(store_status.last_stage, rc);
	}

	k_mutex_unlock(&store_lock);

	return rc;
}

int modbus_register_store_save_now(void)
{
	uint8_t payload[1024];
	struct modbus_store_header active_header;
	struct modbus_store_header new_header;
	uint8_t active_bank = 0U;
	uint8_t target_bank = 0U;
	uint32_t new_sequence = 1U;
	size_t payload_size = 0U;
	int rc;

	k_mutex_lock(&store_lock, K_FOREVER);

	if (store_area == NULL) {
		set_failure(MODBUS_STORE_STAGE_SELECT_BANK, -ENODEV);
		k_mutex_unlock(&store_lock);
		return -ENODEV;
	}

	store_status.last_stage = MODBUS_STORE_STAGE_BUILD_PAYLOAD;
	rc = build_payload(payload, sizeof(payload), &payload_size);
	store_status.last_payload_size = payload_size;
	if (rc != 0) {
		goto out;
	}

	store_status.last_stage = MODBUS_STORE_STAGE_SELECT_BANK;
	if (select_active_bank(&active_bank, &active_header) == 0) {
		target_bank = active_bank == 0U ? 1U : 0U;
		new_sequence = active_header.sequence + 1U;
	} else {
		target_bank = 0U;
		new_sequence = 1U;
	}

	store_status.last_stage = MODBUS_STORE_STAGE_SIZE_CHECK;
	if (sizeof(new_header) + payload_size > bank_size) {
		rc = -ENOSPC;
		goto out;
	}

	store_status.last_stage = MODBUS_STORE_STAGE_ERASE;
	rc = flash_area_erase(store_area, bank_offset(target_bank), bank_size);
	if (rc != 0) {
		goto out;
	}

	store_status.last_stage = MODBUS_STORE_STAGE_WRITE_PAYLOAD;
	rc = flash_area_write(store_area,
			      bank_offset(target_bank) + sizeof(new_header),
			      payload, payload_size);
	if (rc != 0) {
		goto out;
	}

	memset(&new_header, 0, sizeof(new_header));
	new_header.magic = MODBUS_STORE_MAGIC;
	new_header.version = MODBUS_STORE_VERSION;
	new_header.header_size = sizeof(new_header);
	new_header.sequence = new_sequence;
	new_header.payload_size = payload_size;
	new_header.payload_crc32 = crc32_ieee(payload, payload_size);
	new_header.header_crc32 = header_crc(&new_header);

	store_status.last_stage = MODBUS_STORE_STAGE_WRITE_HEADER;
	rc = flash_area_write(store_area, bank_offset(target_bank),
			      &new_header, sizeof(new_header));
	if (rc != 0) {
		goto out;
	}

	store_status.last_stage = MODBUS_STORE_STAGE_VALIDATE;
	rc = validate_bank(target_bank, &active_header);
	if (rc != 0) {
		goto out;
	}

	store_status.dirty = false;
	store_status.active_bank_valid = true;
	store_status.active_bank = target_bank;
	store_status.active_sequence = new_header.sequence;
	store_status.payload_size = payload_size;
	store_status.last_payload_size = payload_size;
	store_status.save_count++;
	store_status.last_stage = MODBUS_STORE_STAGE_NONE;
	store_status.last_error = 0;

out:
	if (rc != 0) {
		set_failure(store_status.last_stage, rc);
		LOG_WRN("Modbus register store save failed: stage=%d rc=%d payload=%u bank_size=%u",
			store_status.last_stage, rc,
			(uint32_t)payload_size, (uint32_t)bank_size);
	}

	k_mutex_unlock(&store_lock);

	return rc;
}

static void save_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	(void)modbus_register_store_save_now();
}

int modbus_register_store_mark_dirty(void)
{
	k_mutex_lock(&store_lock, K_FOREVER);
	store_status.dirty = true;
	k_mutex_unlock(&store_lock);

	return k_work_reschedule(&save_work,
				 K_MSEC(CONFIG_MODBUS_REGISTER_STORE_SAVE_DELAY_MS));
}

int modbus_register_store_clear(void)
{
	int rc;

	k_mutex_lock(&store_lock, K_FOREVER);

	if (store_area == NULL) {
		set_failure(MODBUS_STORE_STAGE_CLEAR, -ENODEV);
		k_mutex_unlock(&store_lock);
		return -ENODEV;
	}

	store_status.last_stage = MODBUS_STORE_STAGE_CLEAR;
	rc = flash_area_erase(store_area, 0, store_area->fa_size);
	if (rc == 0) {
		store_status.dirty = false;
		store_status.active_bank_valid = false;
		store_status.active_bank = 0U;
		store_status.active_sequence = 0U;
		store_status.payload_size = 0U;
		store_status.clear_count++;
		store_status.last_stage = MODBUS_STORE_STAGE_NONE;
		store_status.last_error = 0;
	} else {
		set_failure(MODBUS_STORE_STAGE_CLEAR, rc);
	}

	k_mutex_unlock(&store_lock);

	return rc;
}

int modbus_register_store_init(void)
{
	int rc;

	memset(&store_status, 0, sizeof(store_status));

	store_status.last_stage = MODBUS_STORE_STAGE_OPEN;
	rc = flash_area_open(MODBUS_STORE_AREA_ID, &store_area);
	if (rc != 0) {
		set_failure(MODBUS_STORE_STAGE_OPEN, rc);
		return rc;
	}

	store_status.last_stage = MODBUS_STORE_STAGE_INIT_CHECK;
	if (store_area->fa_size < 8192U ||
	    (store_area->fa_size % MODBUS_STORE_BANK_COUNT) != 0U) {
		set_failure(MODBUS_STORE_STAGE_INIT_CHECK, -EINVAL);
		return -EINVAL;
	}

	bank_size = store_area->fa_size / MODBUS_STORE_BANK_COUNT;
	store_status.bank_size = bank_size;
	store_status.initialized = true;

	rc = modbus_register_store_load();
	if (rc != 0) {
		LOG_WRN("Modbus register store load failed: %d", rc);
	}

	LOG_INF("Modbus register store ready: area=%u bank_size=%u active=%s bank=%u seq=%u",
		MODBUS_STORE_AREA_ID, (uint32_t)bank_size,
		store_status.active_bank_valid ? "yes" : "no",
		store_status.active_bank, store_status.active_sequence);

	return 0;
}

void modbus_register_store_get_status(struct modbus_register_store_status *status)
{
	if (status == NULL) {
		return;
	}

	k_mutex_lock(&store_lock, K_FOREVER);
	*status = store_status;
	k_mutex_unlock(&store_lock);
}

int modbus_register_store_format_status(char *buf, size_t len)
{
	struct modbus_register_store_status status;
	int written;

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	modbus_register_store_get_status(&status);
	written = snprintk(buf, len,
			   "{\"type\":\"modbus_store\",\"initialized\":%s,"
			   "\"dirty\":%s,\"active_bank_valid\":%s,"
			   "\"active_bank\":%u,\"active_sequence\":%u,"
			   "\"bank_size\":%u,\"payload_size\":%u,"
			   "\"last_payload_size\":%u,\"load_count\":%u,"
			   "\"save_count\":%u,\"clear_count\":%u,"
			   "\"fail_count\":%u,\"last_stage\":%d,"
			   "\"last_error\":%d}",
			   status.initialized ? "true" : "false",
			   status.dirty ? "true" : "false",
			   status.active_bank_valid ? "true" : "false",
			   status.active_bank, status.active_sequence,
			   status.bank_size, status.payload_size,
			   status.last_payload_size, status.load_count,
			   status.save_count, status.clear_count,
			   status.fail_count, status.last_stage,
			   status.last_error);

	if (written < 0) {
		return written;
	}

	return (size_t)written >= len ? -EMSGSIZE : 0;
}
