# IMU Sample Service

This module provides a reusable IMU sampling service on top of the
`wit_imu_modbus` helper.

It owns:

- the periodic sampling thread
- latest-sample cache
- success/failure statistics
- callback fanout for product code
- `imu` shell diagnostics

It deliberately does not own:

- product Modbus TCP register names
- system-health event IDs
- power sequencing
- board-specific wiring

Product code should register a callback with:

```c
imu_sample_service_register_callback(callback, user_data);
```

Enable it with:

```text
CONFIG_WIT_IMU_MODBUS=y
CONFIG_IMU_SAMPLE_SERVICE=y
```
