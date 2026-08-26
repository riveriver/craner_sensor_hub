# 编码器采样服务

`encoder_sample_service` 是“一路编码器”的通用采样服务。它不关心编码器品牌、协议细节或业务通道名称，只通过 `struct encoder_sample_backend` 调用具体后端。

## 负责范围

- 为每个 service 实例创建一个周期采样线程。
- 调用 app 传入的 backend 完成初始化、读取和复位。
- 维护最新采样值、成功/失败统计、连续失败次数和读耗时。
- 每次成功或失败后回调 app。
- 注册 `encoder stats` shell 诊断命令。

## 不负责范围

- 不决定回转、变幅、起升等业务通道是否启用。
- 不决定使用哪个 RS485 口、站号、波特率或寄存器地址。
- 不直接写 Modbus TCP 寄存器。
- 不更新系统健康事件。
- 不依赖某个具体编码器型号。

## 使用方式

具体型号放在 `driver/<model>_encoder_modbus/` 或其他 driver/helper 目录中，并导出一个 backend 实例，例如：

```c
extern const struct encoder_sample_backend idecoder_encoder_modbus_backend;
```

应用层创建每一路编码器的 backend client/config，然后启动 service：

```c
static struct encoder_sample_service slewing_service;
static struct idecoder_encoder_modbus_client slewing_client;
static const struct idecoder_encoder_modbus_config slewing_backend_config = {
	.iface_name = CONFIG_ENCODER_SLEWING_IFACE_NAME,
	.unit_id = CONFIG_ENCODER_MODBUS_UNIT_ID,
	.baud = CONFIG_ENCODER_MODBUS_BAUD,
	.rx_timeout_us = CONFIG_ENCODER_MODBUS_RX_TIMEOUT_US,
	.start_addr = CONFIG_ENCODER_MODBUS_START_ADDR,
};

static const struct encoder_sample_service_config slewing_config = {
	.name = "slewing",
	.iface_name = CONFIG_ENCODER_SLEWING_IFACE_NAME,
	.period_ms = CONFIG_ENCODER_SAMPLE_PERIOD_MS,
	.backend = &idecoder_encoder_modbus_backend,
	.backend_client = &slewing_client,
	.backend_config = &slewing_backend_config,
};
```

以后新增第二种编码器时，新增 driver/helper 并导出新的 `encoder_sample_backend`，`encoder_sample_service` 不需要修改。
