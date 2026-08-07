#ifndef COREDUMP_SERVICE_H_
#define COREDUMP_SERVICE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct coredump_service_status {
	bool initialized;
	bool stored_dump_found;
	bool stored_dump_valid;
	size_t stored_dump_size;
	int backend_error;
	int verify_result;
	int last_error;
};

int coredump_service_init(void);
void coredump_service_get_status(struct coredump_service_status *status);
int coredump_service_refresh(void);
int coredump_service_clear_stored_dump(void);
int coredump_service_format_report(char *buf, size_t len);
int coredump_service_read_stored_dump(off_t offset, uint8_t *buf, size_t len);
int coredump_service_format_hex_line(const uint8_t *data, size_t data_len,
				     char *buf, size_t buf_len);

#endif /* COREDUMP_SERVICE_H_ */
