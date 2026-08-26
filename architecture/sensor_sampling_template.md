# 传感器采样架构模板

本项目中的 Modbus/RS485 传感器统一使用三层结构：

```text
driver/<device_type>_modbus/
module/<capability>_sample_service/
app/<product_channel>_app/
```

这套结构的目标是把“设备协议知识”“可复用采样能力”和“产品业务映射”分开。

## 分层职责

### Driver Helper 层

示例：

```text
driver/wit_imu_modbus/
driver/idecoder_encoder_modbus/
driver/anemometer_modbus/
```

这一层表示某一种具体设备类型以及它的通信协议。

它负责：

- 初始化 Modbus client。
- 定义串口帧格式，例如 `8N1` 或 `8E1`。
- 定义单次读取需要访问的寄存器地址和数量。
- 把原始寄存器解码成该设备自己的 sample 结构体。

它不负责：

- 产品业务通道名，例如 `luffing`、`slewing`、`hoisting`。
- 产品具体使用哪一路 RS485。
- 采样周期、重试策略。
- Modbus TCP 寄存器名。
- 系统健康事件。

典型 API：

```c
int device_modbus_init(struct device_modbus_client *client,
		       const struct device_modbus_config *config);
int device_modbus_fetch(struct device_modbus_client *client,
			struct device_modbus_sample *sample);
void device_modbus_reset(struct device_modbus_client *client);
```

### Sample Service 层

示例：

```text
module/imu_sample_service/
module/encoder_sample_service/
module/anemometer_sample_service/
```

这一层提供“单路采样服务”。它应该可以实例化，产品 app 需要几路就创建几个实例。

它负责：

- 每个实例一条采样线程。
- 失败后的重试和底层 helper 重新初始化。
- 每个实例自己的最新采样缓存。
- 每个实例自己的成功/失败统计。
- 每次成功或失败后回调业务 app。
- 通用 shell 诊断命令，例如 `imu stats`、`encoder stats`、`anemometer stats`。

它不负责：

- 产品启用哪些业务通道。
- 产品通道使用哪个 RS485 口、哪个 unit id。
- 写 Modbus TCP 寄存器。
- 系统健康事件 ID。

典型 API：

```c
static struct sensor_sample_service service;

static const struct sensor_sample_service_config cfg = {
	.name = "product_channel",
	.iface_name = "rs485-uartX",
	.unit_id = 1,
	.baud = 9600,
	.rx_timeout_us = 30000,
	.period_ms = 50,
	.start_delay_ms = 0,
};

sensor_sample_service_start(&service, &cfg, sample_cb, user_data);
```

### Product App 层

示例：

```text
app/luffing_imu_app/
app/encoder_app/
app/anemometer_app/
```

这一层决定产品如何使用可复用采样服务。

它负责：

- 决定启用哪些业务通道。
- 决定每个通道绑定哪一路 RS485。
- 定义产品级 Kconfig，例如 `CONFIG_ENCODER_USE_SLEWING`。
- 把采样结果写入本产品的 Modbus TCP input registers。
- 更新本产品的系统健康事件。
- 塔型、产品型号等业务默认值。

它不负责：

- 底层 Modbus 寄存器读取。
- 通用重试逻辑。
- 通用统计逻辑。
- 通用 shell 诊断。

## 命名规则

`driver/` 使用“设备类型 + 协议”命名：

```text
wit_imu_modbus
idecoder_encoder_modbus
anemometer_modbus
```

`module/` 使用“能力 + service”命名：

```text
imu_sample_service
encoder_sample_service
anemometer_sample_service
```

`app/` 使用“产品业务通道 + app”命名：

```text
luffing_imu_app
encoder_app
anemometer_app
```

不要把公司名、项目名或过强的业务名放进可复用的 driver helper 或 sample service。

## Kconfig 放置规则

Driver helper 的 Kconfig 只负责启用该 helper：

```text
CONFIG_WIT_IMU_MODBUS
CONFIG_IDECODER_ENCODER_MODBUS
CONFIG_ANEMOMETER_MODBUS
```

Sample service 的 Kconfig 只负责通用资源限制：

```text
CONFIG_IMU_SAMPLE_SERVICE
CONFIG_IMU_SAMPLE_THREAD_STACK_SIZE
CONFIG_IMU_SAMPLE_SERVICE_MAX_INSTANCES
```

Product app 的 Kconfig 负责业务通道绑定：

```text
CONFIG_ENABLE_LUFFING_IMU_APP
CONFIG_LUFFING_IMU_IFACE_NAME
CONFIG_ENABLE_ANEMOMETER_APP
CONFIG_ANEMOMETER_IFACE_NAME
```

## 当前项目示例

IMU：

```text
driver/wit_imu_modbus
module/imu_sample_service
app/luffing_imu_app
```

编码器：

```text
driver/idecoder_encoder_modbus
module/encoder_sample_service
app/encoder_app
```

风速计：

```text
driver/anemometer_modbus
module/anemometer_sample_service
app/anemometer_app
```

## 新增传感器检查清单

1. 新增 `driver/<device>_modbus/`，提供 `init/fetch/reset`。
2. 新增 `module/<capability>_sample_service/`，提供基于实例的 `start()` API。
3. 新增 `app/<product_channel>_app/`，绑定 RS485、寄存器和健康事件。
4. 在顶层 `CMakeLists.txt` 显式添加 `add_subdirectory()` 和 `target_link_libraries()`。
5. 在顶层 `Kconfig` 添加对应 `rsource`。
6. 在 `prj.conf` 或产品配置片段中启用产品需要的通道。
7. 重构后使用 `rg` 检查旧符号和旧路径是否还有残留。
