# WIT IMU Modbus Helper

This helper owns only the WIT Modbus RTU protocol details:

- initialize a Zephyr Modbus RTU client on a named interface
- read WIT standard-precision angle registers
- read WIT high-precision angle registers
- return roll, pitch, and yaw in millidegrees

It deliberately does not own sampling threads, health events, shell commands,
power sequencing, or product Modbus TCP register mirrors.

Enable it with:

```text
CONFIG_WIT_IMU_MODBUS=y
```
