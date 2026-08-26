# 风速计采样服务

`anemometer_sample_service` 是“一路风速计”的通用采样服务。它不关心风速计型号或协议细节，只通过 `struct anemometer_sample_backend` 调用具体后端。

## 负责范围

- 为每个 service 实例创建一个周期采样线程。
- 调用 app 传入的 backend 完成初始化、读取和复位。
- 维护最新温度、湿度、气压、风速、风向和原始寄存器。
- 维护成功/失败统计、连续失败次数和读耗时。
- 每次成功或失败后回调 app。
- 注册 `anemometer stats` shell 诊断命令。

## 不负责范围

- 不决定业务是否启用风速计。
- 不决定 RS485 口、站号、波特率或寄存器地址。
- 不直接写 Modbus TCP 寄存器。
- 不更新系统健康事件。
- 不依赖某个具体风速计型号。

## 使用方式

具体型号放在 `driver/<model>_anemometer_modbus/` 或其他 driver/helper 目录中，并导出一个 backend 实例，例如：

```c
extern const struct anemometer_sample_backend anemometer_modbus_backend;
```

应用层创建风速计的 backend client/config，然后启动 service：

```c
static struct anemometer_sample_service anemometer_service;
static struct anemometer_modbus_client anemometer_client;
static const struct anemometer_modbus_config anemometer_backend_config = {
	.iface_name = CONFIG_ANEMOMETER_IFACE_NAME,
	.unit_id = CONFIG_ANEMOMETER_MODBUS_UNIT_ID,
	.baud = CONFIG_ANEMOMETER_MODBUS_BAUD,
	.rx_timeout_us = CONFIG_ANEMOMETER_MODBUS_RX_TIMEOUT_US,
	.start_addr = CONFIG_ANEMOMETER_MODBUS_START_ADDR,
	.register_count = CONFIG_ANEMOMETER_MODBUS_REGISTER_COUNT,
};

static const struct anemometer_sample_service_config anemometer_config = {
	.name = "anemometer",
	.iface_name = CONFIG_ANEMOMETER_IFACE_NAME,
	.period_ms = CONFIG_ANEMOMETER_SAMPLE_PERIOD_MS,
	.backend = &anemometer_modbus_backend,
	.backend_client = &anemometer_client,
	.backend_config = &anemometer_backend_config,
};
```

以后新增第二种风速计时，新增 driver/helper 并导出新的 `anemometer_sample_backend`，`anemometer_sample_service` 不需要修改。
