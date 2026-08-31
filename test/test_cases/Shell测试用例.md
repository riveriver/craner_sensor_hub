# Shell 测试用例

实现：Zephyr `SHELL_BACKEND_MQTT`、`module/device_identity_service`、`module/device_param_server`。

本专项验证串口 Shell 和 MQTT Shell 后端的命令执行、设备身份关联、topic 规则、输出内容以及重连行为。MQTT 后端只传输 Shell 命令输出，不应把普通系统日志持续转发到 MQTT。

## 前置条件

- 已烧录启用 `CONFIG_SHELL_BACKEND_MQTT=y` 的测试固件。
- 测试设备已连接以太网并能够访问 MQTT Broker。
- 默认 Broker：`mqtt.craner.hk:1883`。
- 默认账号：`hkcrctest`，密码：`crcHK3130`。正式测试应通过测试组安全配置提供账号，不要把生产密码写入报告。
- 准备串口终端、MQTTX 或等效 MQTT 工具，并允许订阅 `+/sh/tx`。
- 设备身份服务已启用，`short_id` 预期为 6 位十六进制字符串，例如 `363838`。

## Topic 规则

```text
下发命令：<short_id>/sh/rx
接收输出：<short_id>/sh/tx
```

例如设备 `short_id=363838` 时：

```text
363838/sh/rx
363838/sh/tx
```

命令 payload 为 Shell 命令文本，不包含 topic 或换行依赖。MQTT QoS 按当前固件和测试工具配置记录，不能以 QoS 改变为理由判定命令失败。

## 验收概览

| 编号 | 测试内容 | 验收标准 | 结果 |
| --- | --- | --- | --- |
| SHELL-001 | 串口获取 short_id | `id show` 输出 6 位十六进制 `short_id` | |
| SHELL-002 | MQTT 服务器获取 short_id | 从 MQTT 报文 topic 的首段获取并校验 short_id | |
| SHELL-003 | MQTT Shell 命令下发 | `/sh/rx` 命令被设备执行并从 `/sh/tx` 返回 | |
| SHELL-004 | Topic 与设备身份一致性 | MQTT topic、串口 `short_id`、身份信息一致 | |
| SHELL-005 | 参数命令 MQTT 验证 | `param list` 返回身份参数或明确空列表提示 | |
| SHELL-006 | 日志隔离 | MQTT Shell 不持续转发普通日志 | |
| SHELL-007 | MQTT 断线重连 | 网络恢复后 Shell 后端可继续收发命令 | |
| SHELL-008 | 异常命令处理 | 错误命令有响应且不导致设备卡死 | |

## 详细用例

### SHELL-001 串口获取 short_id

1. 通过串口连接设备，等待系统启动完成。
2. 执行：

   ```text
   id show
   ```

3. 记录 `short_id`、`hardware_uid`、`long_id` 和 `mqtt_client_id`。
4. 重启设备后再次执行并比较结果。

预期：

- 输出包含 `short_id: <6位十六进制>`。
- 同一设备重启前后 `short_id` 不变。
- `short_id` 来源于硬件 UID 前 3 个字节，不因重启随机变化。

### SHELL-002 MQTT 服务器获取 short_id

1. 使用 MQTT 工具订阅：

   ```text
   +/sh/tx
   ```

2. 记录收到报文的完整 topic。
3. 取 topic 中 `/sh/tx` 前的首段作为候选 `short_id`。
4. 使用候选 ID 订阅：

   ```text
   <short_id>/sh/tx
   ```

5. 通过已知设备 ID 或串口向 `<short_id>/sh/rx` 发布 `id show`，确认设备能返回结果。

预期：

- topic 首段为 6 位十六进制字符串。
- 精确订阅能够收到该设备的 Shell 输出。
- 不同设备的 topic 首段不能相同。

说明：MQTT 发布端不能向带通配符的 topic 发布。首次从服务器发现 ID 时，通配符只用于订阅；命令下发必须使用具体的 `<short_id>/sh/rx`。

### SHELL-003 MQTT Shell 命令下发

1. 订阅 `<short_id>/sh/tx`。
2. 向 `<short_id>/sh/rx` 依次发布：

   ```text
   id show
   param status
   param list
   ```

3. 保存每条命令对应的 MQTT 输出。

预期：每条命令均在合理时间内收到对应响应，响应内容与串口执行结果一致，设备无异常重启、死锁或 Shell 后端失联。

### SHELL-004 Topic 与设备身份一致性

1. 记录串口 `id show` 的 `short_id`。
2. 从 MQTT 报文记录完整收发 topic。
3. 比较 topic 首段与 `short_id`。

预期：

```text
MQTT topic 首段 == device_identity_service.short_id
```

不得使用 `hardware_uid`、`long_id` 或随机生成值替代 topic 首段。

### SHELL-005 参数命令 MQTT 验证

1. 通过 MQTT 发布 `param list`。
2. 检查返回内容。
3. 启用身份参数服务的固件应验证以下参数：

   ```text
   id.company
   id.model
   id.product
   ```

预期：参数已注册时返回参数和值；没有任何参数时必须返回：

```text
no parameters registered
```

不得出现“命令无响应”的静默情况。

### SHELL-006 日志隔离

1. 订阅 `<short_id>/sh/tx`。
2. 让设备持续运行，并制造可恢复的业务告警，例如暂时断开传感器或 RS485 从站。
3. 观察 MQTT Shell 输出。
4. 通过串口日志或本地日志确认告警确实产生。

预期：

- MQTT `/sh/tx` 只出现 Shell 命令的响应。
- 普通 `wrn`、`err`、业务周期日志不会持续占用 MQTT Shell 带宽。
- Shell 命令执行期间产生的命令输出仍然完整返回。

### SHELL-007 MQTT 断线重连

1. MQTT 连接正常时执行一次 `id show`，确认有响应。
2. 断开网络或阻断 Broker 连接 30 秒。
3. 恢复网络，等待 MQTT 后端重新连接。
4. 再次执行 `param status` 和 `id show`。

预期：设备主业务继续运行；MQTT 恢复后能重新收发 Shell 命令，`short_id` 和 topic 不变化。

### SHELL-008 异常命令处理

1. 发布不存在的命令和错误参数，例如：

   ```text
   command_not_found
   param get unknown.key
   ```

2. 连续重复发送错误命令 20 次。
3. 再执行 `id show`。

预期：每条错误命令返回 Shell 错误或帮助信息；设备不崩溃、不卡死，后续合法命令仍可执行。

## 缺陷证据

必须保存：串口 `id show` 输出、MQTT 客户端订阅截图或报文导出、完整 topic、payload、QoS、时间戳、固件版本、设备 IP、Broker 地址和失败前后的设备状态。
