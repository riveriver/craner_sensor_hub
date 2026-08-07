#include "coredump_service.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/debug/coredump.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

static struct coredump_service_status service_status;

static int hex_nibble_to_char(uint8_t nibble, char *ch)
{
	if (ch == NULL) {
		return -EINVAL;
	}

	if (nibble < 10U) {
		*ch = (char)('0' + nibble);
		return 0;
	}

	if (nibble < 16U) {
		*ch = (char)('a' + nibble - 10U);
		return 0;
	}

	return -EINVAL;
}

int coredump_service_refresh(void)
{
	int rc;

	service_status.backend_error =
		coredump_query(COREDUMP_QUERY_GET_ERROR, NULL);

	rc = coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL);
	if (rc < 0) {
		service_status.stored_dump_found = false;
		service_status.stored_dump_valid = false;
		service_status.stored_dump_size = 0U;
		service_status.verify_result = rc;
		service_status.last_error = rc;
		return rc;
	}

	service_status.stored_dump_found = (rc == 1);
	service_status.stored_dump_size = 0U;
	service_status.verify_result = 0;
	service_status.stored_dump_valid = false;

	if (service_status.stored_dump_found) {
		rc = coredump_query(COREDUMP_QUERY_GET_STORED_DUMP_SIZE, NULL);
		if (rc < 0) {
			service_status.last_error = rc;
			return rc;
		}

		service_status.stored_dump_size = (size_t)rc;

		rc = coredump_cmd(COREDUMP_CMD_VERIFY_STORED_DUMP, NULL);
		service_status.verify_result = rc;
		if (rc < 0) {
			service_status.last_error = rc;
			return rc;
		}

		service_status.stored_dump_valid = (rc == 1);
	}

	service_status.last_error = 0;
	return 0;
}

int coredump_service_init(void)
{
	int rc;

	memset(&service_status, 0, sizeof(service_status));
	service_status.initialized = true;

	rc = coredump_service_refresh();
	if (rc != 0) {
		return rc;
	}

	return 0;
}

void coredump_service_get_status(struct coredump_service_status *status)
{
	if (status == NULL) {
		return;
	}

	*status = service_status;
}

int coredump_service_clear_stored_dump(void)
{
	int rc;
	int clear_rc;

	rc = coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL);
	if (rc != 0) {
		service_status.last_error = rc;
		return rc;
	}

	clear_rc = coredump_cmd(COREDUMP_CMD_CLEAR_ERROR, NULL);
	if (clear_rc != 0 && clear_rc != -ENOTSUP) {
		service_status.last_error = clear_rc;
		return clear_rc;
	}

	return coredump_service_refresh();
}

int coredump_service_format_report(char *buf, size_t len)
{
	struct coredump_service_status status;
	int written;
	int rc;

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	rc = coredump_service_refresh();
	coredump_service_get_status(&status);
	if (rc != 0) {
		return rc;
	}

	written = snprintk(buf, len,
			   "{\"type\":\"coredump\","
			   "\"found\":%s,\"valid\":%s,"
			   "\"size\":%u,\"backend_error\":%d,"
			   "\"verify_result\":%d,\"last_error\":%d,"
			   "\"uptime_ms\":%lld}",
			   status.stored_dump_found ? "true" : "false",
			   status.stored_dump_valid ? "true" : "false",
			   (uint32_t)status.stored_dump_size,
			   status.backend_error, status.verify_result,
			   status.last_error, (long long)k_uptime_get());
	if (written < 0) {
		return written;
	}

	if ((size_t)written >= len) {
		return -EMSGSIZE;
	}

	return 0;
}

int coredump_service_read_stored_dump(off_t offset, uint8_t *buf, size_t len)
{
	struct coredump_cmd_copy_arg copy = {
		.offset = offset,
		.buffer = buf,
		.length = len,
	};

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	return coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &copy);
}

int coredump_service_format_hex_line(const uint8_t *data, size_t data_len,
				     char *buf, size_t buf_len)
{
	size_t out_len = strlen("#CD:") + (data_len * 2U);
	size_t pos = 0U;
	int rc;

	if (data == NULL || buf == NULL) {
		return -EINVAL;
	}

	if (buf_len <= out_len) {
		return -EMSGSIZE;
	}

	memcpy(buf, "#CD:", strlen("#CD:"));
	pos = strlen("#CD:");

	for (size_t i = 0U; i < data_len; i++) {
		rc = hex_nibble_to_char(data[i] >> 4, &buf[pos]);
		if (rc != 0) {
			return rc;
		}
		pos++;

		rc = hex_nibble_to_char(data[i] & 0x0fU, &buf[pos]);
		if (rc != 0) {
			return rc;
		}
		pos++;
	}

	buf[pos] = '\0';
	return 0;
}
