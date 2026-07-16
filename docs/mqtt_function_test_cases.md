# MQTT 功能测试用例

本文档面向测试组，用于对已经烧录好固件、已经封盒的整机进行 MQTT 功能验收。测试组不需要构建固件、烧录固件、打开外壳、连接调试器或使用串口，主要使用 MQTTX 和以太网完成测试；Telnet Shell 只作为辅助观察和参数配置入口。

本文档只覆盖 MQTT 连接、上线状态、MQTT 日志、MQTT 白名单远程诊断命令、断网重连和 MQTT 压力稳定性。基础网络测试见 [network_function_performance_test_cases.md](network_function_performance_test_cases.md)，Modbus TCP 测试见 [modbus_tcp_test_cases.md](modbus_tcp_test_cases.md)，通用持久化存储测试见 [persistent_storage_service_test_cases.md](persistent_storage_service_test_cases.md)。

## 1. 测试范围

1. MQTT 连接管理：验证设备网络 ready 后可连接指定 broker，并使用设备身份生成的 MQTT Client ID。
2. 在线状态：验证设备连接 MQTT 后发布 retained online status。
3. MQTT 日志：验证设备日志发布到 `craner/{project}/{type}/log/emb`。
4. MQTT 远程诊断：验证 MQTTX 可通过 request topic 发送白名单命令，并从 response topic 收到响应。
5. MQTT 远程维护：验证参数查询、参数设置、CoreDump 查询/导出/清除等允许的远程维护命令。
6. MQTT 恢复能力：验证断网、broker 不可达和设备重启后 MQTT 可自动恢复。
7. MQTT 性能稳定性：验证连续命令、日志接收和基础并发场景下设备稳定。

## 2. 结果状态定义

1. `通过`：实际结果与预期结果一致。
2. `失败`：连接状态、topic、payload、响应内容、恢复时间或稳定性结果与预期不一致；必须记录缺陷编号。
3. `阻塞`：因 MQTT broker、测试账号、网络、防火墙、MQTTX 或供电条件等外部原因导致无法执行。
4. `不适用`：当前设备没有 CoreDump，或本轮测试不允许执行清除、恢复出厂、重启等破坏性/影响性命令。

## 3. 测试准备

1. 被测设备：
   已经烧录正式测试固件、已经封盒的整机。测试组不能使用串口、SWD/J-Link/ST-Link，也不能重新烧录固件。

2. 通用主机名格式：

   ```text
   craner-{project}-{type}-{name_uid}.local
   ```

   默认值通常为：

   ```text
   craner-project-type-<name_uid>.local
   ```

3. 开发人员必须提供的信息：

   ```text
   设备 mDNS 主机名:
   固件版本:
   MQTT broker host:
   MQTT broker port:
   MQTT username:
   MQTT password:
   MQTT request topic:
   MQTT response topic:
   MQTT log topic:
   MQTT online status topic:
   本轮测试是否允许 param_set:
   本轮测试是否允许 param_save:
   本轮测试是否允许 param_factory_reset:
   本轮测试是否允许 coredump_clear:
   本轮测试是否允许 reboot:
   ```

4. 当前代码默认 MQTT 信息：

   ```text
   Broker host: mqtt.craner.hk
   Broker port: 1883
   Username: hkcrctest
   Password: crcHK3130
   Request topic: craner/encoder_hub/cmd/request
   Response topic: craner/encoder_hub/cmd/response
   Log topic: craner/{project}/{type}/log/emb
   Online status topic: craner/{project}/{type}/{uid}/status/online
   MQTT Client ID: craner-{project}-{type}-{name_uid}
   ```

   正式测试以开发人员提供的信息为准。密码属于敏感信息，测试记录中可只记录账号名和连接配置截图，不建议明文公开密码。

5. 测试工具：

   ```text
   MQTTX: MQTT 发布/订阅
   MobaXterm: Telnet Shell，辅助执行 net_status、time_status、param_get 等命令
   Windows PowerShell: ping、Test-NetConnection
   ```

6. MQTTX 连接配置：

   ```text
   Name: craner-mqtt-test
   Host: mqtt.craner.hk
   Port: 1883
   Username: hkcrctest
   Password: <由开发人员提供>
   Client ID: qa-pc-<测试人员或工位编号>
   Clean Session: true
   ```

   MQTTX 的 Client ID 不能与设备 MQTT Client ID 相同，避免互相踢下线。

## 4. 验收总表

测试组先根据下表进行总体验收记录，再按后续 MQTT-xx 用例执行详细测试。`验收结果` 一栏由测试组填写，只能填写 `通过`、`失败`、`阻塞` 或 `不适用`；失败时填写缺陷编号。

| 编号 | 测试内容 | 验收标准 | 验收结果 |
| --- | --- | --- | --- |
| MQTT-01 | 测试资料输入检查 | 开发人员已提供 broker、账号、topic、固件版本和允许执行的维护动作。 |  |
| MQTT-02 | MQTTX 连接 broker | MQTTX 使用测试账号可连接 broker。 |  |
| MQTT-03 | 设备 MQTT 在线状态 | MQTTX 可收到设备 retained online status，topic 和 payload 符合规则。 |  |
| MQTT-04 | MQTT Client ID | online payload 中 `mqtt_client_id` 等于 `craner-{project}-{type}-{name_uid}`。 |  |
| MQTT-05 | MQTT 日志 topic | MQTTX 可在 `craner/{project}/{type}/log/emb` 收到设备日志。 |  |
| MQTT-06 | MQTT 远程命令 mqtt_status | 发布 `mqtt_status` 后 response topic 收到成功响应。 |  |
| MQTT-07 | MQTT 远程命令 net_status | 发布 `net_status` 后 response topic 收到网络状态响应。 |  |
| MQTT-08 | 白名单拒绝非授权命令 | 发布非白名单命令后设备返回 error/unsupported，且不执行命令。 |  |
| MQTT-09 | 参数查询 | 发布 `param_get device/project` 后只返回 key 和 value 等必要信息。 |  |
| MQTT-10 | 参数修改但不保存 | 允许时发布 `param_set` 后运行时生效，但未 `param_save` 前重启不应持久化。 |  |
| MQTT-11 | 参数保存与重启恢复 | 允许时 `param_set` + `param_save` 后重启，参数保持保存值。 |  |
| MQTT-12 | CoreDump 摘要查询 | 发布 `coredump_report` 后收到摘要；无 CoreDump 时 found/valid/size 表示为空状态。 |  |
| MQTT-13 | CoreDump 完整导出 | 有 CoreDump 且允许时发布 `coredump_export`，可收到 `#CD:BEGIN#`、分片和 `#CD:END#`。 |  |
| MQTT-14 | CoreDump 清除 | 允许时发布 `coredump_clear` 后再次查询为空。 |  |
| MQTT-15 | 断网后 MQTT 恢复 | 断网恢复后 MQTT 自动重连，远程命令重新可用。 |  |
| MQTT-16 | Broker 不可达恢复 | Broker 或网络恢复后设备可重新连接并发布在线状态。 |  |
| MQTT-17 | 重启后 MQTT 恢复 | 设备重启后重新上线，MQTT 远程命令可用。 |  |
| MQTT-18 | 连续命令稳定性 | MQTTX 连续发送 50 次 `mqtt_status`，响应成功率 100%。 |  |
| MQTT-19 | MQTT 与 Telnet 并发 | MQTTX 连续命令和 Telnet 查询同时运行 30 分钟，设备不崩溃。 |  |

## 5. 详细测试用例

### MQTT-01 测试资料输入检查

1. 测试目的：
   确认 MQTT 测试开始前，测试组已经获得 broker、账号、topic 和维护权限边界。

2. 前置条件：
   被测设备已经上电并接入测试网络。

3. 测试步骤：
   检查开发人员是否已提供第 3 章列出的全部信息。

4. 预期结果：
   MQTT 连接信息和 topic 完整，破坏性或影响性命令是否允许执行已经明确。

5. 判定标准：
   信息完整判定通过；缺少 broker、账号、topic 或权限边界时，相关测试记录为阻塞。

### MQTT-02 MQTTX 连接 broker

1. 测试目的：
   验证测试 PC 的 MQTTX 能连接 broker，排除测试环境问题。

2. 前置条件：
   PC 可访问 MQTT broker，账号密码已由开发人员提供。

3. 测试步骤：
   按第 3 章配置 MQTTX，连接 broker。

4. 预期结果：
   MQTTX 显示 connected。

5. 判定标准：
   MQTTX 可连接 broker 判定通过；无法连接时记录为阻塞，并附 MQTTX 错误截图。

### MQTT-03 设备 MQTT 在线状态

1. 测试目的：
   验证设备 MQTT 连接成功后发布 retained online status。

2. 前置条件：
   MQTTX 已连接 broker，设备已接入网络。

3. 测试步骤：
   在 MQTTX 订阅开发人员提供的 online status topic，例如：

   ```text
   craner/{project}/{type}/{uid}/status/online
   ```

   如果不确定 `{uid}`，可先订阅：

   ```text
   craner/+/+/+/status/online
   ```

4. 预期结果：
   MQTTX 收到 retained online status。payload 包含：

   ```text
   company
   project
   device_type
   uid
   hostname
   mdns
   mqtt_client_id
   mac
   ip
   gateway
   time_valid
   time
   uptime_ms
   ```

5. 判定标准：
   topic 和 payload 字段符合规则判定通过。

### MQTT-04 MQTT Client ID

1. 测试目的：
   验证设备 MQTT Client ID 与设备身份规则一致。

2. 前置条件：
   MQTT-03 已收到 online status。

3. 测试步骤：
   查看 online status payload 中的 `mqtt_client_id`、`hostname` 和 `mdns`。

4. 预期结果：

   ```text
   mqtt_client_id: craner-{project}-{type}-{name_uid}
   hostname: craner-{project}-{type}-{name_uid}
   mdns: craner-{project}-{type}-{name_uid}.local
   ```

5. 判定标准：
   MQTT Client ID 与 hostname 一致，并符合当前设备身份规则。

### MQTT-05 MQTT 日志 topic

1. 测试目的：
   验证设备日志发布到由开机读取的 project/type 生成的 topic。

2. 前置条件：
   MQTTX 已连接 broker，开发人员已提供当前设备 project/type。

3. 测试步骤：
   在 MQTTX 订阅：

   ```text
   craner/{project}/{type}/log/emb
   ```

   通过 Telnet Shell 执行非破坏性状态命令，例如：

   ```text
   net_status
   time_status
   ```

   如果当前日志等级下没有新日志，等待设备产生 warning/error，或由开发人员指定非破坏性日志触发方法。

4. 预期结果：
   MQTTX 可在指定 log topic 收到设备日志。运行时修改 `device/project` 或 `device/type` 后，不要求当前启动周期立刻切换 topic；保存并重启后才按新参数生效。

5. 判定标准：
   日志 topic 与本次开机读取的 project/type 一致判定通过。

### MQTT-06 MQTT 远程命令 mqtt_status

1. 测试目的：
   验证 MQTT 白名单远程诊断命令通道可用。

2. 前置条件：
   MQTTX 已连接 broker，并订阅 response topic。

3. 测试步骤：
   订阅：

   ```text
   craner/encoder_hub/cmd/response
   ```

   发布：

   ```text
   Topic: craner/encoder_hub/cmd/request
   Payload: mqtt_status
   ```

4. 预期结果：
   response topic 收到 JSON 响应，`status` 为 `ok`，`cmd` 为 `mqtt_status`。

5. 判定标准：
   收到成功响应判定通过。

### MQTT-07 MQTT 远程命令 net_status

1. 测试目的：
   验证 MQTT 远程命令可查询网络状态。

2. 前置条件：
   MQTT-06 已通过。

3. 测试步骤：
   发布：

   ```text
   Topic: craner/encoder_hub/cmd/request
   Payload: net_status
   ```

4. 预期结果：
   response topic 收到包含网络状态的响应，能看到 ready、IP 或等价状态信息。

5. 判定标准：
   响应内容可判断设备网络状态判定通过。

### MQTT-08 白名单拒绝非授权命令

1. 测试目的：
   验证 MQTT 远程诊断不是完整 Shell 透传，非白名单命令不会被执行。

2. 前置条件：
   MQTT-06 已通过。

3. 测试步骤：
   发布一个非白名单命令，例如：

   ```text
   Topic: craner/encoder_hub/cmd/request
   Payload: kernel threads
   ```

4. 预期结果：
   response topic 收到 error/unsupported 类响应，设备不执行该命令。

5. 判定标准：
   非白名单命令被拒绝判定通过。

### MQTT-09 参数查询

1. 测试目的：
   验证 MQTT 远程命令可查询设备参数，并且输出只包含测试关心的信息。

2. 前置条件：
   MQTT-06 已通过。

3. 测试步骤：
   发布：

   ```text
   Topic: craner/encoder_hub/cmd/request
   Payload: param_get device/project
   ```

4. 预期结果：
   response topic 收到成功响应，message 中包含 key 和 value。示例：

   ```json
   {"status":"ok","cmd":"param_get","message":"device/project=project"}
   ```

5. 判定标准：
   能读取指定参数，且不混入大量内部无关字段，判定通过。

### MQTT-10 参数修改但不保存

1. 测试目的：
   验证 `param_set` 只修改运行时 RAM 镜像，未 `param_save` 前不应持久化。

2. 前置条件：
   本轮测试允许执行 `param_set` 和 `reboot`，且开发人员已指定可临时修改的参数和值。

3. 测试步骤：
   发布：

   ```text
   Payload: param_set shell/output_format json
   Payload: param_get shell/output_format
   ```

   确认运行时值变为 `json` 后，不执行 `param_save`，再通过允许的方式重启设备。

4. 预期结果：
   重启前参数运行时值改变；未保存重启后恢复为上一次保存值。

5. 判定标准：
   未保存不持久化判定通过。

6. 恢复步骤：
   如测试后格式影响人工读取，可执行：

   ```text
   Payload: param_set shell/output_format kv
   Payload: param_save
   ```

### MQTT-11 参数保存与重启恢复

1. 测试目的：
   验证 `param_set` + `param_save` 后参数可持久化。

2. 前置条件：
   本轮测试允许执行 `param_set`、`param_save` 和 `reboot`。

3. 测试步骤：
   发布：

   ```text
   Payload: param_set shell/output_format json
   Payload: param_save
   Payload: reboot
   ```

   设备恢复 MQTT 后再发布：

   ```text
   Payload: param_get shell/output_format
   ```

4. 预期结果：
   重启后 `shell/output_format` 仍为 `json`。

5. 判定标准：
   保存值重启后仍存在判定通过。

6. 恢复步骤：
   测试结束后按项目要求恢复默认：

   ```text
   Payload: param_set shell/output_format kv
   Payload: param_save
   ```

### MQTT-12 CoreDump 摘要查询

1. 测试目的：
   验证 MQTT 可手动触发 CoreDump 摘要查询。

2. 前置条件：
   MQTT-06 已通过。

3. 测试步骤：
   发布：

   ```text
   Payload: coredump_report
   ```

   同时订阅开发人员提供的 CoreDump/日志发布 topic。

4. 预期结果：
   response topic 返回 `published` 或错误原因；发布 topic 收到摘要，包含 `found`、`valid`、`size`、`backend_error` 等字段。

5. 判定标准：
   有 CoreDump 时能看到 found/valid/size；无 CoreDump 时能明确表示空状态，不误报。

### MQTT-13 CoreDump 完整导出

1. 测试目的：
   验证 MQTT 可按分片导出完整 CoreDump 内容，用于真正 debug。

2. 前置条件：
   设备存在 CoreDump，本轮测试允许导出。若当前无 CoreDump，本用例记录为 `不适用`。

3. 测试步骤：
   订阅 CoreDump/日志发布 topic，发布：

   ```text
   Payload: coredump_export
   ```

4. 预期结果：
   MQTTX 收到：

   ```text
   #CD:BEGIN#
   #CD:<hex>
   #CD:<hex>
   #CD:END#
   ```

5. 判定标准：
   能收到开始标记、连续 hex 分片和结束标记判定通过。

### MQTT-14 CoreDump 清除

1. 测试目的：
   验证 MQTT 远程维护可在允许时清除已保存 CoreDump。

2. 前置条件：
   本轮测试允许执行 `coredump_clear`。如果不允许，记录为 `不适用`。

3. 测试步骤：
   发布：

   ```text
   Payload: coredump_clear
   Payload: coredump_report
   ```

4. 预期结果：
   清除命令返回成功；再次查询时 `found=false` 或等价空状态。

5. 判定标准：
   CoreDump 被清除且不会自动重新出现判定通过。

### MQTT-15 断网后 MQTT 恢复

1. 测试目的：
   验证设备断网恢复后 MQTT 自动重连。

2. 前置条件：
   本轮测试允许断网，MQTT-06 已通过。

3. 测试步骤：
   拔出设备网线，等待 30 秒后插回。MQTTX 保持订阅 response topic 和 online status topic。网络恢复后发布：

   ```text
   Payload: mqtt_status
   ```

4. 预期结果：
   断网期间 MQTT 不可用或连接断开；恢复后设备重新上线，`mqtt_status` 可收到响应。

5. 判定标准：
   MQTT 自动恢复判定通过。记录实测恢复时间。

### MQTT-16 Broker 不可达恢复

1. 测试目的：
   验证 broker 或网络路径短时不可达后设备可重试并恢复。

2. 前置条件：
   测试环境允许临时阻断 broker，或使用可控测试 broker。

3. 测试步骤：
   临时阻断设备到 broker 的访问，等待设备进入重试；恢复 broker 访问后观察 online status 和远程命令响应。

4. 预期结果：
   broker 不可达时设备不崩溃；恢复后设备重新连接，并可响应 `mqtt_status`。

5. 判定标准：
   broker 恢复后 MQTT 可自动恢复判定通过。

### MQTT-17 重启后 MQTT 恢复

1. 测试目的：
   验证设备重启后 MQTT 自动重新上线。

2. 前置条件：
   本轮测试允许重启。

3. 测试步骤：
   通过 Telnet 或 MQTT 发布：

   ```text
   Payload: reboot
   ```

   设备重启后，MQTTX 观察 online status topic，并发布：

   ```text
   Payload: mqtt_status
   ```

4. 预期结果：
   设备重启后重新连接 broker，online status 更新，远程命令恢复可用。

5. 判定标准：
   重启后 MQTT 恢复判定通过。记录实测恢复时间。

### MQTT-18 连续命令稳定性

1. 测试目的：
   验证 MQTT 远程诊断命令在连续请求下稳定。

2. 前置条件：
   MQTT-06 已通过。

3. 测试步骤：
   使用 MQTTX 或测试脚本连续发送 50 次：

   ```text
   Payload: mqtt_status
   ```

   每次发送间隔建议 1 秒。

4. 预期结果：
   50 次均收到 response topic 响应，无设备重启、断连或明显卡死。

5. 判定标准：
   响应成功率 100% 判定通过。

### MQTT-19 MQTT 与 Telnet 并发

1. 测试目的：
   验证 MQTT 远程诊断和 Telnet 维护入口同时使用时设备稳定。

2. 前置条件：
   MQTT-06 和网络测试 NET-05 已通过。

3. 测试步骤：
   同时执行：

   ```text
   MQTTX 每 10 秒发布一次 mqtt_status
   MobaXterm Telnet 每 1 分钟执行一次 net_status
   PC 持续 ping 设备
   ```

   持续 30 分钟。

4. 预期结果：
   设备不崩溃，MQTT 响应正常，Telnet 可用，ping 无异常连续丢包。

5. 判定标准：
   并发运行 30 分钟稳定判定通过。

## 6. MQTT 性能指标建议

如果项目暂未定义 MQTT 性能阈值，可先使用以下建议作为测试记录参考，后续由项目正式确认：

| 指标 | 建议初始验收参考 |
| --- | --- |
| MQTTX `mqtt_status` 响应成功率 | 100% |
| MQTTX 连续 50 次命令响应 | 无丢响应、无崩溃 |
| MQTT 断网恢复 | 记录实测恢复时间，后续固化阈值 |
| MQTT 重启恢复 | 记录实测恢复时间，后续固化阈值 |
| MQTT 日志接收 | topic 正确，日志格式可读 |

## 7. 缺陷记录要求

1. MQTTX 无法连接 broker 时，记录 broker host、port、账号名、网络环境、MQTTX 错误截图。
2. 设备无 online status 时，记录设备主机名、IP、`net_status`、MQTTX 订阅 topic 和等待时间。
3. 远程命令无响应时，记录 request topic、payload、response topic、发送时间和 MQTTX 截图。
4. topic 不正确时，记录 `param_get device/project`、`param_get device/type`、实际订阅 topic、收到消息的 topic 和设备重启时间点。
5. CoreDump 导出异常时，记录收到的 `#CD` 分片数量、是否有 begin/end 标记、设备是否仍在线。
6. 断网或重启恢复异常时，记录断网/重启时间点、恢复等待时间、online status 是否更新和远程命令是否恢复。
7. 涉及清除、恢复出厂、重启等操作时，必须在测试记录中写明是否得到本轮测试授权。
