# IMU 采样服务

`imu_sample_service` 是“一路 IMU”的通用采样服务。它只认识 `struct imu_sample_backend`，不直接依赖 WIT 或其他具体 IMU 型号。

## 负责范围

- 为每个 IMU service 实例创建一个周期采样线程。
- 调用 app 传入的 backend 完成初始化、读取和复位。
- 缓存最新姿态角采样值、原始寄存器、在线状态和错误码。
- 维护成功/失败统计、连续失败次数和读耗时。
- 每次成功或失败后回调 app。
- 注册 `imu sample` 和 `imu stats` shell 诊断命令。

## 不负责范围

- 不决定业务通道名称，例如变幅 IMU。
- 不决定 RS485 口、站号、波特率、精度模式等后端参数。
- 不直接写 Modbus TCP 寄存器。
- 不更新系统健康事件。
- 不处理板级供电、GPIO 或安装位置业务逻辑。

## 使用方式

具体 IMU 型号放在 `driver/<model>_imu_modbus/` 或其他 driver/helper 目录中，并导出一个 backend 实例，例如：

```c
extern const struct imu_sample_backend wit_imu_modbus_backend;
```

应用层创建每一路 IMU 的 backend client/config，然后启动 service：

```c
static struct imu_sample_service luffing_imu_service;
static struct wit_imu_modbus_client luffing_imu_client;
static const struct wit_imu_modbus_config luffing_imu_backend_config = {
	.iface_name = CONFIG_LUFFING_IMU_IFACE_NAME,
	.model = WIT_IMU_MODBUS_MODEL_STANDARD_PRECISION,
	.unit_id = CONFIG_LUFFING_IMU_MODBUS_UNIT_ID,
	.baud = CONFIG_LUFFING_IMU_MODBUS_BAUD,
	.rx_timeout_us = CONFIG_LUFFING_IMU_MODBUS_RX_TIMEOUT_US,
};

static const struct imu_sample_service_config luffing_imu_config = {
	.name = "luffing_imu",
	.iface_name = CONFIG_LUFFING_IMU_IFACE_NAME,
	.period_ms = CONFIG_LUFFING_IMU_SAMPLE_PERIOD_MS,
	.backend = &wit_imu_modbus_backend,
	.backend_client = &luffing_imu_client,
	.backend_config = &luffing_imu_backend_config,
};
```

以后新增第二种 IMU 时，新增 driver/helper 并导出新的 `imu_sample_backend`，`imu_sample_service` 不需要修改。
