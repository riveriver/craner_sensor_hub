# 传感器采样架构模板

本文档记录当前项目采用的传感器采样分层方式。后续新增编码器、IMU、风速计或同类传感器型号时，按这个模板扩展。

## 总体分层

```text
module/<sensor>_sample_service/
  一路通用采样服务
  backends/<model>_<sensor>_<bus>/
    具体型号和协议插件

app/<business>_app/
  产品业务装配
```

核心原则是：`backend plugin` 提供具体型号采样能力，`sample_service` 执行一路采样，`app` 决定业务实例和结果去向。后端插件随 sample service 子模块一起分发，方便多个项目复用同一套服务和型号支持。

## backend plugin 层

backend plugin 放在 `module/<sensor>_sample_service/backends/<model>_<sensor>_<bus>/`。

负责：

- 初始化具体通信后端，例如 Modbus RTU client。
- 读取具体型号的寄存器或数据帧。
- 解析型号专属数据格式、比例系数和精度模式。
- 导出该类传感器的 backend 实例。
- 随同类 `sample_service` 子模块一起进入其他项目。

不负责：

- 创建采样线程。
- 维护采样统计。
- 绑定业务通道。
- 写产品寄存器。
- 更新系统健康事件。

示例：

```c
const struct encoder_sample_backend idecoder_encoder_modbus_backend = {
	.name = "idecoder_encoder_modbus",
	.init = idecoder_encoder_backend_init,
	.fetch = idecoder_encoder_backend_fetch,
	.reset = idecoder_encoder_backend_reset,
};
```

## sample service 层

sample service 放在 `module/<sensor>_sample_service/`，表示“一路某类传感器采样服务”。

当前目录结构采用中等粒度拆分：

```text
module/encoder_sample_service/
  encoder_sample_backend.h
  encoder_sample_service.h
  encoder_sample_service_internal.h
  encoder_sample_service.c
  encoder_sample_service_stats.c
  encoder_sample_service_shell.c
  backends/
    idecoder_encoder_modbus/
      idecoder_encoder_modbus.c
      idecoder_encoder_modbus.h
      Kconfig
      README.md
```

职责：

- `*_sample_service.h`：对 app 暴露 public API。
- `*_sample_backend.h`：对 backend plugin 暴露 backend ops 和通用 sample 结构。
- `*_sample_service_internal.h`：service 内部 `.c` 文件共享声明，外部不要 include。
- `*_sample_service.c`：模块入口、`start()`、`get_latest()`、`get_stats()`、采样线程和 backend 调度。
- `*_sample_service_stats.c`：成功/失败状态更新、latest sample 缓存、可选统计累计。
- `*_sample_service_shell.c`：shell 诊断命令，可通过 Kconfig 裁剪，当前依赖统计功能。

service 只认识同类传感器的 backend 接口，例如：

```c
struct encoder_sample_backend {
	const char *name;
	int (*init)(void *client, const void *config);
	int (*fetch)(void *client, struct encoder_sample_service_sample *sample);
	void (*reset)(void *client);
};
```

service core 不知道具体品牌、型号、寄存器表、业务通道或健康事件。service CMake 负责 `add_subdirectory(backends/...)` 并把可选后端 target 作为 `INTERFACE` 依赖传递给产品 app。

## app 层

app 放在 `app/<business>_app/`，是产品装配层。

负责：

- 决定启用哪些业务通道，例如回转、变幅、起升。
- 决定每一路绑定哪个 RS485 名称。
- 决定每一路使用哪个 backend。
- 准备每一路 backend client/config。
- 处理采样回调。
- 写产品 Modbus TCP 寄存器。
- 更新系统健康事件。

示例：

```c
static struct encoder_sample_service slewing_encoder_service;
static struct idecoder_encoder_modbus_client slewing_encoder_client;
static const struct idecoder_encoder_modbus_config slewing_backend_config = {
	.iface_name = CONFIG_ENCODER_SLEWING_IFACE_NAME,
	.unit_id = CONFIG_ENCODER_MODBUS_UNIT_ID,
	.baud = CONFIG_ENCODER_MODBUS_BAUD,
	.rx_timeout_us = CONFIG_ENCODER_MODBUS_RX_TIMEOUT_US,
	.start_addr = CONFIG_ENCODER_MODBUS_START_ADDR,
};

static const struct encoder_sample_service_config slewing_encoder_config = {
	.name = "slewing",
	.iface_name = CONFIG_ENCODER_SLEWING_IFACE_NAME,
	.period_ms = CONFIG_ENCODER_SAMPLE_PERIOD_MS,
	.backend = &idecoder_encoder_modbus_backend,
	.backend_client = &slewing_encoder_client,
	.backend_config = &slewing_backend_config,
};
```

## 可裁剪能力

每类 service 保留两个推荐裁剪项：

```text
CONFIG_ENCODER_SAMPLE_SERVICE_SHELL
CONFIG_ENCODER_SAMPLE_SERVICE_STATS
CONFIG_ENCODER_SAMPLE_SERVICE_REINIT_ON_ENODEV

CONFIG_IMU_SAMPLE_SERVICE_SHELL
CONFIG_IMU_SAMPLE_SERVICE_STATS
CONFIG_IMU_SAMPLE_SERVICE_REINIT_ON_ENODEV

CONFIG_ANEMOMETER_SAMPLE_SERVICE_SHELL
CONFIG_ANEMOMETER_SAMPLE_SERVICE_STATS
CONFIG_ANEMOMETER_SAMPLE_SERVICE_REINIT_ON_ENODEV
```

`*_SHELL` 控制是否编译 shell 诊断命令。量产版本不需要串口诊断时可以关闭。

`*_STATS` 控制是否累计成功次数、失败次数、连续失败次数、平均读耗时和最大读耗时。关闭后 service 仍然更新 latest sample 并执行 callback，但 `get_stats()` 返回的统计结构保持清零。当前 shell 诊断依赖统计功能。

`*_REINIT_ON_ENODEV` 控制当 backend 返回 `-ENODEV` 时，service 是否调用 `backend->reset()` 并在下一轮采样重新初始化。这个功能适合 RS485/Modbus 设备掉线、串口接口异常、设备热插拔或现场干扰恢复场景，默认建议开启。

## 当前项目实例

编码器：

```text
module/encoder_sample_service/backends/idecoder_encoder_modbus/
  idecoder_encoder_modbus_backend

module/encoder_sample_service/
  encoder_sample_backend
  encoder_sample_service

app/encoder_app/
  决定回转、变幅、起升通道
  决定每一路 RS485 和 backend
  写编码器输入寄存器
  更新编码器健康事件
```

IMU：

```text
module/imu_sample_service/backends/wit_imu_modbus/
  wit_imu_modbus_backend

module/imu_sample_service/
  imu_sample_backend
  imu_sample_service

app/luffing_imu_app/
  决定变幅 IMU 通道
  决定 RS485 和 backend
  写 IMU 输入寄存器
  更新健康事件
```

风速计：

```text
module/anemometer_sample_service/backends/anemometer_modbus/
  anemometer_modbus_backend

module/anemometer_sample_service/
  anemometer_sample_backend
  anemometer_sample_service

app/anemometer_app/
  决定风速计通道
  决定 RS485 和 backend
  写风速计输入寄存器
  更新健康事件
```

## 新增同类传感器型号

以新增第二种编码器为例：

1. 新增 `module/encoder_sample_service/backends/xxx_encoder_modbus/`。
2. 实现 `xxx_encoder_modbus_init()`、`xxx_encoder_modbus_fetch()`、`xxx_encoder_modbus_reset()`。
3. 导出 `xxx_encoder_modbus_backend`。
4. 在 `module/encoder_sample_service/Kconfig` 中 `rsource` 新后端 Kconfig。
5. 在 `app/encoder_app/Kconfig` 的 backend choice 中新增型号选项，并 `select XXX_ENCODER_MODBUS`。
6. 在 `app/encoder_app/encoder_app.c` 中准备对应 client/config。
7. 将目标通道的 service config 指向 `.backend = &xxx_encoder_modbus_backend`。
8. 不修改 `module/encoder_sample_service/` 的核心 `.c` 文件。

IMU 和风速计同理：在对应 service 的 `backends/` 下新增具体插件，导出该类 backend，在 app 中选择并装配，service core 保持不变。

## 命名约定

- `module/<sensor>_sample_service/backends/<model>_<sensor>_<bus>/`：具体型号和协议插件，例如 `idecoder_encoder_modbus`。
- `module/<sensor>_sample_service/`：某一类传感器的一路采样服务，例如 `encoder_sample_service`。
- `app/<business>_app/`：产品业务装配，例如 `encoder_app`、`luffing_imu_app`。
- DTS 外设节点优先按硬件能力命名，例如 `rs485-uart8`，不要把 Modbus、传感器用途或业务含义写死到硬件节点名里。

## 判断代码该放哪里

- 只和传感器型号、协议解析有关：放对应 sample service 的 `backends/`。
- 和一路采样线程、统计、回调、shell 诊断有关：放 `module/<sensor>_sample_service/`。
- 和产品通道、寄存器表、健康事件、业务启用策略有关：放 `app/`。
- 和全局寄存器表、系统健康事件定义等产品公共能力有关：保留在产品公共代码中。
