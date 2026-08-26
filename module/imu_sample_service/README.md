# IMU Sample Service

This module provides a reusable IMU sampling service on top of the
`wit_imu_modbus` helper.

It owns:

- one periodic sampling thread per service instance
- latest-sample cache per instance
- success/failure statistics per instance
- one callback per instance
- `imu` shell diagnostics

It deliberately does not own:

- the product channel name
- the RS485 interface used by a product channel
- the IMU model chosen by a product channel
- product Modbus TCP register names
- system-health event IDs
- power sequencing
- board-specific wiring

Product code creates one instance per IMU channel:

```c
static struct imu_sample_service luffing_imu;

static const struct imu_sample_service_config cfg = {
	.name = "luffing_imu",
	.iface_name = "rs485-uart8",
	.model = WIT_IMU_MODBUS_MODEL_STANDARD_PRECISION,
	.unit_id = 80,
	.baud = 9600,
	.rx_timeout_us = 50000,
	.period_ms = 50,
};

imu_sample_service_start(&luffing_imu, &cfg, callback, user_data);
```

Enable it with:

```text
CONFIG_WIT_IMU_MODBUS=y
CONFIG_IMU_SAMPLE_SERVICE=y
```
