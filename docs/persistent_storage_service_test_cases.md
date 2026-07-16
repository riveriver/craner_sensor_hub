# 持久化存储服务测试用例

本文档面向测试组，用于对已经烧录好固件、已经封盒的整机进行持久化存储服务验收。测试组不需要构建固件、烧录固件、打开外壳、连接调试器或使用串口，只通过以太网完成测试，并输出每条用例的测试结果和缺陷记录。

本文档只测试持久化存储服务本身，包括外部 W25Q64 分区状态、设备参数 `param-store`、Shell 输出格式、CoreDump 查询/导出接口、MQTT 维护接口，以及清除操作的隔离性。Modbus TCP 通信和 Modbus 寄存器持久化测试见 [modbus_tcp_test_cases.md](modbus_tcp_test_cases.md)。

## 1. 测试范围

1. 外部 W25Q64：通过 `storage_status` 验证 `param-store` 和 `modbus-store` 分区可被系统识别，设备启动后状态正常。

2. 设备参数持久化：验证 `param_get`、`param_set`、`param_save`、`param_factory_reset` 的保存、重启恢复、非法值拒绝和输出格式切换。

3. Shell 输出格式：验证 `shell/output_format` 控制 Telnet Shell 输出格式，并且保存后重启仍生效。

4. CoreDump：验证封盒整机可通过 Telnet 或 MQTT 查询、导出和清除已有 CoreDump。产测不主动制造崩溃，不要求测试组触发 fault。

5. MQTT 维护：验证 MQTT 远程参数维护和 CoreDump 维护接口与本地 Telnet Shell 行为一致。

6. 操作隔离：验证 `coredump_clear` 不影响设备参数；验证 `param_factory_reset` 不误清除 CoreDump。

## 2. 测试准备

1. 被测设备准备：

   测试对象为已经烧录好正式测试固件、已经封盒的整机。测试组不打开外壳，不连接 SWD/J-Link/ST-Link，不使用串口，不重新烧录固件。开发人员在测试前提供设备 mDNS 主机名、MQTT 维护 topic、测试账号和本轮测试要求保留或清除的数据范围。

2. 网络准备：

   设备使用 DHCP 获取 IP。整机产测时不能使用串口，测试人员应先在 PC 上使用 mDNS 主机名发现设备：

   ```powershell
   ping craner-{project}-{type}-{name_uid}.local
   ```

   测试报告中必须记录实际使用的主机名和解析到的 IP 地址。

3. Telnet Shell 准备：

   整机产测 Shell 统一通过以太网 Telnet 访问，不使用串口。测试工具使用 MobaXterm，新建 Telnet Session：

   ```text
   Remote host: craner-{project}-{type}-{name_uid}.local
   Port: 23
   Terminal: Telnet
   ```

   连接成功后应进入 Zephyr Shell，并可执行：

   ```text
   net_status
   storage_status
   ```

4. MQTTX 测试准备：

   MQTT 远程维护测试统一使用 MQTTX。测试前需要开发人员提供以下信息：

   ```text
   Broker host:
   Broker port:
   Username:
   Password:
   MQTT shell request topic:
   MQTT shell response topic:
   CoreDump report/export topic:
   ```

   在 MQTTX 中新建连接，填写 broker 地址、端口、用户名和密码。连接成功后，先订阅 MQTT shell response topic 和 CoreDump report/export topic，再向 MQTT shell request topic 发布维护命令。

5. 初始清理建议：

   如果本轮测试允许清除历史数据，建议测试开始前通过 MobaXterm Telnet Shell 执行以下命令，保证测试基线干净：

   ```text
   param_factory_reset
   coredump_clear
   reboot
   ```

   `param_factory_reset` 和 `coredump_clear` 都是破坏性维护命令。执行前必须确认本轮测试允许清除历史数据。如果 Telnet Shell 执行 `reboot` 后连接断开，等待设备重启完成后在 MobaXterm 中重新连接 Telnet。

## 3. 当前已知设备参数

当前设备参数表包含：

```text
time/sync_mode
time/ntp_server
mqtt/host
mqtt/port
mqtt/username
device/project
device/type
shell/output_format
```

## 4. 测试结果记录要求

1. 每条测试用例必须记录测试结果，结果只能填写 `通过`、`失败`、`阻塞` 或 `不适用`。

2. `通过` 表示实际结果与预期结果一致。

3. `失败` 表示设备响应、保存结果、重启恢复结果或维护命令输出与预期不一致。失败时必须记录缺陷编号。

4. `阻塞` 表示因为网络环境、测试账号、MobaXterm、MQTTX、MQTT broker、供电条件等外部原因导致用例无法执行。阻塞时必须记录阻塞原因。

5. `不适用` 表示当前封盒整机没有对应测试条件，例如设备当前没有 CoreDump，无法执行 CoreDump 完整导出验证。该情况不按缺陷处理，但必须记录原因。

6. 每条失败用例至少记录以下证据：设备主机名、设备 IP、测试时间、MobaXterm Telnet 输出、相关 Shell 命令输出、MQTTX 连接信息、MQTTX 发布 payload 和订阅响应、重启或断电操作时间点。

## 5. 测试验收总表

测试组先根据下表进行总体验收记录，再按后续 PSS-xx 用例执行详细测试。`验收结果` 一栏由测试组填写，只能填写 `通过`、`失败`、`阻塞` 或 `不适用`；失败时填写缺陷编号。

| 编号 | 测试内容 | 验收标准 | 验收结果 |
| --- | --- | --- | --- |
| PSS-01 | 整机网络联通性检查 | PC 可通过 `craner-{project}-{type}-{name_uid}.local` ping 通设备；MobaXterm Telnet 可进入 Shell；`net_status` 与 `storage_status` 可正常返回。 |  |
| PSS-02 | 存储分区状态检查 | `storage_status` 显示 `initialized=yes`、`internal_flash_ready=yes`、`external_flash_ready=yes`，`param_store` 与 `modbus_store` 均 available，`last_error=0`。 |  |
| PSS-03 | Shell 输出格式切换 | `shell/output_format` 可在 `kv` 与 `json` 间切换，运行时立即生效，执行 `param_save` 后重启仍保持。 |  |
| PSS-04 | 设备参数保存和重启恢复 | 修改 `mqtt/host`、`mqtt/port` 后执行 `param_save`，重启后参数仍为保存值。 |  |
| PSS-05 | 设备参数未保存不持久化 | 执行 `param_set` 但不执行 `param_save`，重启后参数恢复为原已保存值。 |  |
| PSS-06 | 设备参数非法值拒绝 | 非法枚举、越界端口、非法 project/type 均被拒绝，原参数值不被覆盖。 |  |
| PSS-07 | 恢复出厂参数 | 执行 `param_factory_reset` 后设备参数恢复默认值，重启后仍保持默认值。 |  |
| PSS-08 | JSON 格式下存储状态 | `shell/output_format=json` 时，`storage_status` 和 `param_status` 输出合法 JSON，字段可被脚本解析。 |  |
| PSS-09 | CoreDump 空状态查询 | 无 CoreDump 时 `coredump_status` 不误报，`coredump_report` 输出空状态摘要。 |  |
| PSS-10 | CoreDump 已有数据导出和清除 | 有 CoreDump 时可导出、可清除；无 CoreDump 时记录不适用，不判失败。 |  |
| PSS-11 | CoreDump 清除不影响设备参数 | 执行 `coredump_clear` 后，已保存设备参数不丢失。 |  |
| PSS-12 | 参数恢复出厂不影响 CoreDump | 如果设备已有 CoreDump，执行 `param_factory_reset` 后 CoreDump 状态仍保持，不被误清除。 |  |
| PSS-13 | MQTTX 参数远程维护 | 使用 MQTTX 发布 `param_get`、`param_set`、`param_save`，响应 topic 可收到结果，参数重启后保持。 |  |
| PSS-14 | MQTTX CoreDump 远程维护 | 使用 MQTTX 发布 CoreDump 状态查询、摘要上报、导出和清除命令；无 CoreDump 时空状态正确。 |  |

## 6. 测试用例

### PSS-01 整机网络联通性检查

1. 测试目的：

   确认封盒整机已启动，mDNS 主机名可解析，以太网通信正常，测试组可以通过 Telnet 访问设备。

2. 前置条件：

   设备已经烧录好固件并封盒，设备接入产测网络，PC 与设备在同一网络或可互通网络内。

3. 测试步骤：

   在 PC 上执行：

   ```powershell
   ping craner-{project}-{type}-{name_uid}.local
   ```

   使用 MobaXterm 新建 Telnet Session：

   ```text
   Remote host: craner-{project}-{type}-{name_uid}.local
   Port: 23
   Terminal: Telnet
   ```

   连接成功后执行：

   ```text
   net_status
   storage_status
   ```

4. 预期结果：

   PC 能 ping 通设备主机名；MobaXterm Telnet 能进入 Shell；`net_status` 显示网络 ready；`storage_status` 能正常返回。

5. 判定标准：

   ping、Telnet Shell、基础状态命令均成功，判定通过。任一步失败都记录为联通性缺陷，并附上 ping 输出、MobaXterm 错误信息和设备主机名。

### PSS-02 存储分区状态检查

1. 测试目的：

   确认设备启动后内部 Flash 和外部 W25Q64 分区被正确识别。

2. 前置条件：

   MobaXterm Telnet Shell 可用。

3. 测试步骤：

   执行：

   ```text
   storage_status
   ```

4. 预期结果：

   `kv` 输出下应看到类似字段：

   ```text
   initialized=yes
   internal_flash_ready=yes
   external_flash_ready=yes
   coredump.available=yes
   app_storage.available=yes
   param_store.available=yes
   modbus_store.available=yes
   param_store.size=131072
   modbus_store.size=131072
   last_error=0
   ```

5. 判定标准：

   内部 Flash ready、外部 Flash ready、`param-store` available、`modbus-store` available，且 `last_error=0`。

### PSS-03 Shell 输出格式切换

1. 测试目的：

   验证 Shell 输出格式由设备参数控制，并且运行时立即生效。

2. 前置条件：

   MobaXterm Telnet Shell 可用。

3. 测试步骤：

   执行：

   ```text
   param_get shell/output_format
   param_set shell/output_format json
   storage_status
   param_get shell/output_format
   param_save
   reboot
   param_get shell/output_format
   storage_status
   param_set shell/output_format kv
   param_save
   ```

4. 预期结果：

   初始默认值通常为：

   ```text
   shell/output_format=kv
   ```

   执行 `param_set shell/output_format json` 后，后续 `storage_status` 应输出 JSON。重启后 `param_get shell/output_format` 应仍然输出 JSON 格式。切回 `kv` 并保存后，后续输出恢复为 `key=value` 风格。

5. 判定标准：

   运行时立即切换，保存后重启仍保持，切回 `kv` 后恢复人类可读输出。

### PSS-04 设备参数保存和重启恢复

1. 测试目的：

   验证 `param-store` 可保存设备参数，断电或重启后参数不丢失。

2. 前置条件：

   MobaXterm Telnet Shell 可用，外部 Flash 状态正常。

3. 测试步骤：

   执行：

   ```text
   param_get mqtt/host
   param_get mqtt/port
   param_set mqtt/host test-broker.local
   param_set mqtt/port 1884
   param_status
   param_save
   param_status
   reboot
   param_get mqtt/host
   param_get mqtt/port
   ```

4. 预期结果：

   `param_set` 后 `param_status` 应显示 `dirty=yes`；`param_save` 后 `dirty=no`，`save_count` 增加。重启后：

   ```text
   mqtt/host=test-broker.local
   mqtt/port=1884
   ```

5. 判定标准：

   参数保存成功，重启后仍为保存值。

6. 恢复步骤：

   ```text
   param_set mqtt/host mqtt.craner.hk
   param_set mqtt/port 1883
   param_save
   ```

### PSS-05 设备参数未保存不持久化

1. 测试目的：

   验证 `param_set` 只修改 RAM 镜像，未执行 `param_save` 时重启不应持久化。

2. 前置条件：

   已知当前 `mqtt/port` 为 `1883` 或其他稳定值。

3. 测试步骤：

   执行：

   ```text
   param_get mqtt/port
   param_set mqtt/port 1885
   param_get mqtt/port
   reboot
   param_get mqtt/port
   ```

4. 预期结果：

   重启前读取为 `1885`；重启后恢复为重启前已保存的值，例如：

   ```text
   mqtt/port=1883
   ```

5. 判定标准：

   未执行 `param_save` 的修改不落盘。

### PSS-06 设备参数非法值拒绝

1. 测试目的：

   验证参数服务会拒绝非法值，不允许非法值进入持久化存储。

2. 前置条件：

   MobaXterm Telnet Shell 可用。

3. 测试步骤：

   分别执行：

   ```text
   param_set shell/output_format xml
   param_set mqtt/port 0
   param_set mqtt/port 70000
   param_set device/project bad_name_with_underscore
   param_set device/type bad_name_with_underscore
   param_status
   ```

4. 预期结果：

   每条非法设置均返回错误，例如：

   ```text
   set parameter failed: -22
   ```

   或 `mqtt/port` 越界时返回范围错误。`param_status` 的 `fail_count` 可能增加，原参数值不应被非法值覆盖。

5. 判定标准：

   非法值无法保存，重启后也不会出现非法值。

### PSS-07 恢复出厂参数

1. 测试目的：

   验证 `param_factory_reset` 会清除 `param-store` 中已保存参数，并恢复默认值。

2. 前置条件：

   已通过 PSS-04 保存过非默认参数。

3. 测试步骤：

   执行：

   ```text
   param_get mqtt/host
   param_factory_reset
   param_get mqtt/host
   param_get shell/output_format
   reboot
   param_get mqtt/host
   param_get shell/output_format
   ```

4. 预期结果：

   执行恢复出厂后，参数恢复为固件默认值。重启后仍为默认值。`shell/output_format` 应恢复默认 `kv`。

5. 判定标准：

   保存过的参数被清除，默认值生效，重启后保持默认值。

### PSS-08 JSON 格式下存储状态

1. 测试目的：

   验证 `storage_status` 和 `param_status` 在 JSON 输出格式下可被脚本解析。

2. 前置条件：

   MobaXterm Telnet Shell 可用。

3. 测试步骤：

   执行：

   ```text
   param_set shell/output_format json
   storage_status
   param_status
   param_set shell/output_format kv
   ```

4. 预期结果：

   `storage_status` 和 `param_status` 输出合法 JSON，字段名稳定，脚本可解析。

5. 判定标准：

   输出为合法 JSON，且至少包含初始化状态、ready 状态、dirty 状态和 last_error 字段。

### PSS-09 CoreDump 空状态查询

1. 测试目的：

   验证无 CoreDump 时查询和上报摘要不误报。

2. 前置条件：

   如果本轮测试允许清除历史 CoreDump，建议先执行：

   ```text
   coredump_clear
   reboot
   ```

3. 测试步骤：

   执行：

   ```text
   coredump_status
   coredump_report
   ```

4. 预期结果：

   无 CoreDump 时 `coredump_status` 应显示：

   ```text
   stored_dump_found: no
   stored_dump_valid: no
   stored_dump_size: 0
   ```

   `coredump_report` 输出摘要，`found=false` 或等价字段。

5. 判定标准：

   无 CoreDump 时不会误报有效 dump。

### PSS-10 CoreDump 已有数据导出和清除

1. 测试目的：

   验证封盒整机在已经存在 CoreDump 时，可以通过 Telnet Shell 手动导出和清除。产测不主动制造崩溃。

2. 前置条件：

   MobaXterm Telnet Shell 可用。执行本用例前先运行 `coredump_status`，确认设备是否已有 CoreDump。

3. 测试步骤：

   执行：

   ```text
   coredump_status
   ```

   如果状态显示 `stored_dump_found: yes`，继续执行：

   ```text
   coredump_report
   coredump_export
   coredump_clear
   coredump_status
   ```

4. 预期结果：

   如果设备已有 CoreDump，首次查询应显示：

   ```text
   stored_dump_found: yes
   stored_dump_valid: yes
   stored_dump_size: 大于 0
   ```

   `coredump_export` 应输出完整 CoreDump 分片，包含：

   ```text
   #CD:BEGIN#
   #CD:<hex>
   #CD:END#
   ```

   清除后再次查询应显示 `stored_dump_found: no`。

   如果设备没有 CoreDump，`coredump_status` 应显示 `stored_dump_found: no`。这种情况记录为“不适用”，不判失败。

5. 判定标准：

   已有 CoreDump 时可导出、可清除；无 CoreDump 时状态正确且不误报。

### PSS-11 CoreDump 清除不影响设备参数

1. 测试目的：

   验证执行 `coredump_clear` 后，设备参数不被误清除。

2. 前置条件：

   MobaXterm Telnet Shell 可用。

3. 测试步骤：

   执行：

   ```text
   param_set mqtt/port 1884
   param_save
   coredump_clear
   reboot
   param_get mqtt/port
   ```

4. 预期结果：

   重启后：

   ```text
   mqtt/port=1884
   ```

5. 判定标准：

   清除 CoreDump 不影响设备参数。

6. 恢复步骤：

   ```text
   param_set mqtt/port 1883
   param_save
   ```

### PSS-12 参数恢复出厂不影响 CoreDump

1. 测试目的：

   验证 `param_factory_reset` 不会误清除 CoreDump。

2. 前置条件：

   设备已有 CoreDump。如果设备没有 CoreDump，本用例记录为“不适用”。

3. 测试步骤：

   执行：

   ```text
   coredump_status
   param_factory_reset
   coredump_status
   ```

4. 预期结果：

   如果恢复出厂前存在 CoreDump，恢复出厂后 `stored_dump_found` 仍为 `yes`，CoreDump 不被误清除。

5. 判定标准：

   参数恢复出厂只影响设备参数，不影响 CoreDump。

### PSS-13 MQTTX 参数远程维护

1. 测试目的：

   验证使用 MQTTX 可通过 MQTT shell 远程读取、设置、保存和恢复设备参数。

2. 前置条件：

   设备网络和 MQTT 已连接。测试人员已在 MQTTX 中连接 broker，并订阅 MQTT shell response topic。

3. 测试步骤：

   在 MQTTX 中向 MQTT shell request topic 依次发布以下 payload：

   ```text
   param_get mqtt/host
   param_set mqtt/port 1884
   param_get mqtt/port
   param_save
   ```

   重启设备后再发布：

   ```text
   param_get mqtt/port
   ```

4. 预期结果：

   MQTTX 订阅的 response topic 应收到 JSON 响应。重启后再次发布 `param_get mqtt/port`，响应中仍为 `1884`。

5. 判定标准：

   MQTTX 远程参数维护与本地 Telnet Shell 行为一致。

6. 恢复步骤：

   ```text
   param_set mqtt/port 1883
   param_save
   ```

### PSS-14 MQTTX CoreDump 远程维护

1. 测试目的：

   验证使用 MQTTX 可通过 MQTT shell 远程查询、上报、导出和清除 CoreDump。

2. 前置条件：

   MQTT 已连接。测试人员已在 MQTTX 中订阅 MQTT shell response topic 和 CoreDump report/export topic。若设备当前没有 CoreDump，只测试 `coredump_status`、`coredump_report` 和 `coredump_clear` 的空状态行为；若设备已有 CoreDump，再测试 `coredump_export`。

3. 测试步骤：

   在 MQTTX 中向 MQTT shell request topic 依次发布以下 payload：

   ```text
   coredump_status
   coredump_report
   coredump_export
   coredump_clear
   coredump_status
   ```

4. 预期结果：

   MQTTX 的 response topic 中应收到命令响应。`coredump_status` 返回摘要状态；`coredump_report` 会触发向 CoreDump report/export topic 发布摘要；`coredump_export` 会触发完整 CoreDump 分片导出；`coredump_clear` 后再次查询应显示不存在 CoreDump。

5. 判定标准：

   远程维护命令均有响应，并且实际状态变化正确。若设备没有 CoreDump，导出步骤记录为“不适用”。

## 7. 自动化测试建议

1. Shell 自动化：

   测试脚本可以通过 Telnet 发送命令，并按行匹配关键字段。人工测试时使用 MobaXterm Telnet Session 执行同样命令。例如 `storage_status` 至少匹配：

   ```text
   initialized=yes
   external_flash_ready=yes
   last_error=0
   ```

2. JSON 自动化：

   建议自动化开始时执行：

   ```text
   param_set shell/output_format json
   ```

   然后解析 `storage_status` 和 `param_status` 的 JSON 输出。测试结束前恢复：

   ```text
   param_set shell/output_format kv
   param_save
   ```

## 8. 缺陷记录建议

1. 如果设备启动失败、Telnet Shell 无响应，记录 MobaXterm Telnet 连接信息、PC ping 结果和最后一次操作。

2. 如果 `storage_status` 中 `external_flash_ready=no`，记录完整 `storage_status` 输出、设备主机名、设备 IP、测试时间、是否刚发生过断电或恢复出厂操作。测试组不拆机检查 W25Q64，由开发或硬件人员后续定位。

3. 如果 `param_save` 失败，记录 `param_status`、`storage_status`、外部 Flash 状态。

4. 如果 CoreDump 查询、导出或清除失败，记录 `coredump_status`、`storage_status`、Telnet/MQTT 命令输出。

5. 如果 MQTT 维护命令返回 unsupported，先确认该命令是否在本轮测试范围内；若需求要求支持，则记录为需求缺口。
