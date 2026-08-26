# 传感器采样架构模板

本文档记录当前项目采用的传感器采样分层方式，后续新增编码器、IMU、风速计或同类传感器型号时按这个模板扩展。

## 分层职责

### driver/helper

放在 `driver/<model>_<sensor>_<bus>/`。

负责具体型号和协议细节，例如：

- Modbus RTU client 初始化。
- 寄存器地址、寄存器数量、原始值解析。
- 型号专属的比例系数、精度模式、异常码处理。
- 导出一个该类传感器的 backend 实例。

不负责：

- 创建采样线程。
- 绑定业务通道。
- 写产品寄存器。
- 更新健康事件。

### sample service

放在 `module/<sensor>_sample_service/`。

负责“一路传感器采样服务”，例如：

- `encoder_sample_service`
- `imu_sample_service`
- `anemometer_sample_service`

service 只依赖该类传感器的 backend 接口，例如：

```c
struct encoder_sample_backend {
	const char *name;
	int (*init)(void *client, const void *config);
	int (*fetch)(void *client, struct encoder_sample_service_sample *sample);
	void (*reset)(void *client);
};
```

service 负责：

- 周期采样线程。
- 失败重试和必要时重新初始化。
- 最新值缓存。
- 成功/失败统计。
- shell 诊断命令。
- 每次采样后的回调。

service 不负责：

- 知道具体品牌或型号。
- 知道业务通道含义。
- 知道产品寄存器映射。
- 知道健康事件 ID。

### app

放在 `app/<business>_app/`。

app 是产品装配层，负责决定：

- 启用哪些业务通道，例如回转、变幅、起升。
- 每个通道绑定哪个 RS485 名称。
- 每个通道使用哪个 backend。
- backend 的型号参数、站号、波特率、超时、寄存器地址。
- 采样成功后写哪些 Modbus TCP 寄存器。
- 采样成功后更新哪个系统健康事件。

## 当前项目实例

### 编码器

```text
driver/idecoder_encoder_modbus/
  idecoder_encoder_modbus_backend

module/encoder_sample_service/
  encoder_sample_backend
  encoder_sample_service

app/encoder_app/
  决定回转、变幅、起升是否启用
  决定每一路使用哪个 RS485
  决定每一路使用哪个 encoder backend
  写编码器输入寄存器
  更新编码器健康事件
```

### IMU

```text
driver/wit_imu_modbus/
  wit_imu_modbus_backend

module/imu_sample_service/
  imu_sample_backend
  imu_sample_service

app/luffing_imu_app/
  决定是否启用变幅 IMU
  决定使用哪个 RS485
  决定使用哪个 IMU backend
  写 IMU 输入寄存器
  更新对应健康事件
```

### 风速计

```text
driver/anemometer_modbus/
  anemometer_modbus_backend

module/anemometer_sample_service/
  anemometer_sample_backend
  anemometer_sample_service

app/anemometer_app/
  决定是否启用风速计
  决定使用哪个 RS485
  决定使用哪个 anemometer backend
  写风速计输入寄存器
  更新风速计健康事件
```

## 新增同类传感器型号的步骤

以新增第二种编码器为例：

1. 新增目录 `driver/xxx_encoder_modbus/`。
2. 实现 `xxx_encoder_modbus_init()`、`xxx_encoder_modbus_fetch()`、`xxx_encoder_modbus_reset()`。
3. 在 driver 中导出：

```c
const struct encoder_sample_backend xxx_encoder_modbus_backend = {
	.name = "xxx_encoder_modbus",
	.init = xxx_encoder_backend_init,
	.fetch = xxx_encoder_backend_fetch,
	.reset = xxx_encoder_backend_reset,
};
```

4. 在 `app/encoder_app/Kconfig` 的 backend choice 中新增型号选项，并 `select XXX_ENCODER_MODBUS`。
5. 在 `app/encoder_app/encoder_app.c` 中为该 backend 准备 client/config，并把 service config 改为：

```c
.backend = &xxx_encoder_modbus_backend,
.backend_client = &xxx_encoder_client,
.backend_config = &xxx_encoder_backend_config,
```

6. 不修改 `module/encoder_sample_service/`。

IMU 和风速计同理：新增具体 driver/helper，导出该类 backend，在 app 中选择并装配，service 保持不变。

## 命名约定

- `driver/<model>_<sensor>_<bus>/`：具体型号和协议，例如 `idecoder_encoder_modbus`、`wit_imu_modbus`。
- `module/<sensor>_sample_service/`：某一类传感器的一路采样服务，例如 `encoder_sample_service`。
- `app/<business>_app/`：产品业务装配，例如 `encoder_app`、`luffing_imu_app`、`anemometer_app`。
- DTS 外设节点优先按硬件能力命名，例如 `rs485-uart8`，不要把 Modbus、传感器用途或业务含义写死到硬件节点名里。

## 判断代码该放哪里

- 只和传感器型号、协议解析有关：放 `driver/`。
- 和一路采样线程、统计、回调、shell 诊断有关：放 `module/<sensor>_sample_service/`。
- 和产品通道、寄存器表、健康事件、业务启用策略有关：放 `app/`。
- 和全局寄存器表、系统健康事件定义等产品公共能力有关：保留在产品公共代码中。
