# 当前版本功能实现清单

本文档用于总结当前代码已经实现的主要功能，方便后续按功能项逐一生成测试用例。本文档不是测试用例本身，也不是市场发布说明，而是面向开发、测试和验收沟通的功能索引。

当前代码基于 Zephyr 应用 `craner_encoder_hub`，目标板为 `craner_general_stm32h743vit6`。当前已启用以太网、DHCP、mDNS、Telnet Shell、Modbus TCP、Modbus RTU 编码器采集、MQTT、NTP/RTC 时间管理、持久化存储、CoreDump、系统健康监控和 MCUboot OTA 控制等功能。

## 1. 总体启动流程

当前 `main.c` 的启动顺序已经实现为：

1. 生成设备身份，包括短 UID、MAC、hostname、mDNS 名称和 MQTT Client ID。

2. 初始化存储服务，检查内部 Flash 和外部 W25Q64 分区状态。

3. 初始化设备参数服务，从外部 `param-store` 加载 settings/NVS 参数。

4. 初始化 Shell 应用，读取并缓存 Shell 输出格式。

5. 初始化 Modbus 寄存器持久化服务，从外部 `modbus-store` 自动加载持久化寄存器。

6. 初始化 CoreDump 诊断服务。

7. 初始化网络服务和时间服务。

8. 手动启动网络，等待 DHCP 获取地址。

可生成测试用例：

1. 上电后设备能完成初始化，不崩溃。

2. 网络起来后 Telnet Shell 可连接。

3. `storage_status`、`net_status`、`time_status` 等基础命令可用。

4. 启动后不会因为外部 Flash 空白、无 CoreDump、RTC 无效而阻塞系统启动。

## 2. 设备身份服务

当前已实现 `device_identity_service`，用于生成设备网络和 MQTT 身份。

已实现功能：

1. 从 STM32 hardware info 读取设备唯一 ID。

2. 使用 FNV-1a 64-bit hash 生成 5 字节 `short_uid`。

3. 生成本地管理 MAC 地址：

   ```text
   02:<short_uid 5 bytes>
   ```

4. 设置以太网接口 MAC 地址。

5. 设置 hostname，其中 `<name_uid>` 为 5 字节 short UID 的最后 2 字节：

   ```text
   craner-{project}-{type}-{name_uid}
   ```

   `project` 和 `type` 来自设备参数，默认值分别为 `project` 和 `type`。

6. 生成 mDNS 名称：

   ```text
   craner-{project}-{type}-{name_uid}.local
   ```

7. 生成 MQTT Client ID：

   ```text
   craner-{project}-{type}-{name_uid}
   ```

8. 对外提供设备 hostname、mDNS 名称、short UID、MAC、MQTT Client ID。

可生成测试用例：

1. 设备每次重启后 MAC 地址保持一致。

2. 同一设备 short UID 保持一致。

3. 不同设备 short UID 和 MAC 应不同。

4. PC 可通过 mDNS 名称访问设备。

5. MQTT 连接使用生成的 Client ID。

## 3. 以太网和 DHCP 网络服务

当前已实现 `network_service`，网络采用手动启动流程，不使用 Zephyr 自动网络初始化。

已实现功能：

1. 使用以太网 IPv4。

2. 使用 DHCPv4 获取 IP 地址，不使用静态 IP。

3. 支持 DHCP 状态管理：

   ```text
   down
   link_up
   dhcp_waiting
   ready
   failed
   ```

4. 支持网络事件监听：

   ```text
   IF_UP
   IF_DOWN
   ETHERNET_CARRIER_ON
   ETHERNET_CARRIER_OFF
   DHCP_START
   DHCP_BOUND
   DHCP_STOP
   IPV4_ADDR_ADD
   IPV4_ADDR_DEL
   ```

5. DHCP 获取成功后记录：

   ```text
   ip
   netmask
   gateway
   dhcp_server
   lease_s
   renew_s
   ```

6. DHCP 失败或地址丢失后支持指数退避重试。

7. 默认 DHCP 初始重试间隔为 `5000 ms`。

8. 默认 DHCP 最大重试间隔为 `60000 ms`。

9. 提供 `net_status` Shell 命令。

可生成测试用例：

1. DHCP 正常场景：设备获取 IP，`net_status` 显示 `ready=yes`。

2. 网线未插场景：`link_up=no`，状态不应误报 ready。

3. DHCP 服务器不可用场景：状态进入等待或失败，重试间隔递增。

4. DHCP 成功后 PC 可 ping 设备。

5. DHCP 租约信息显示正确。

6. 断网再恢复后设备可重新 ready。

## 4. Telnet Shell 和本地维护命令

当前启用了 Zephyr Shell：

```text
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_SHELL_BACKEND_TELNET=y
```

产测和封盒整机测试推荐使用 Telnet Shell。

当前项目新增或封装的 Shell 命令包括：

1. 固件和基础状态：

   ```text
   fw_time
   net_status
   storage_status
   ```

2. 设备参数：

   ```text
   param_status
   param_get [key]
   param_set <key> <value>
   param_save
   param_factory_reset
   ```

3. Modbus 持久化存储：

   ```text
   modbus_store_status
   modbus_store_save
   modbus_store_load
   modbus_store_clear
   ```

4. CoreDump：

   ```text
   coredump_status
   coredump_clear
   coredump_report
   coredump_export
   ```

5. 时间服务：

   ```text
   time_status
   time_sync
   time_mode
   time_set
   ```

6. RTC：

   ```text
   rtc_status
   rtc_get
   rtc_set
   rtc_trust_clear
   ```

7. 测试构建中启用的故障注入：

   ```text
   fault_oops
   ```

   注意：`CONFIG_CRANER_ENABLE_FAULT_INJECTION_SHELL=y` 当前在 `prj.conf` 中启用。正式量产固件应关闭。

可生成测试用例：

1. Telnet Shell 连接成功。

2. 每个维护命令可执行并返回合理结果。

3. 破坏性命令需要单独测试并明确恢复步骤。

4. `fault_oops` 仅测试构建允许使用，正式固件应验证不可用。

## 5. Shell 输出格式控制

当前 Shell 状态类命令支持两种输出风格：

```text
kv
json
```

控制参数：

```text
shell/output_format
```

已实现功能：

1. 默认输出格式为 `kv`。

2. `param_set shell/output_format json` 后立即影响运行时输出。

3. `param_save` 后重启仍保持输出格式。

4. Shell 模块开机时读取一次参数并缓存，后续普通状态命令不反复访问 settings。

可生成测试用例：

1. 默认输出格式为 `kv`。

2. 设置为 `json` 后 `storage_status`、`param_status`、`modbus_store_status` 输出 JSON。

3. 保存后重启仍保持 JSON。

4. 切回 `kv` 后恢复人类可读输出。

## 6. 设备参数持久化服务

当前已实现 `device_param_store`，用于保存设备可配置参数。

已实现功能：

1. 参数服务使用 Zephyr `settings + NVS`。

2. settings/NVS 挂载到外部 W25Q64 的 `param-store` 分区。

3. `param_set` 只修改 RAM 镜像并标记 dirty。

4. `param_save` 才写入外部 Flash。

5. `param_factory_reset` 清除已保存参数并恢复默认值。

6. 参数值有类型和合法性校验。

7. 支持状态查询和 JSON 格式输出。

当前参数表：

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

可生成测试用例：

1. 参数默认值查询。

2. 参数设置但不保存，重启后不持久化。

3. 参数设置并保存，重启后恢复保存值。

4. 非法参数值被拒绝。

5. 恢复出厂后参数回到默认值。

6. 外部 Flash 正常时 `settings_ready=yes`。

## 7. 持久化存储分区服务

当前已实现 `storage_service`，用于探测内部 Flash 和外部 W25Q64 固定分区。

已实现功能：

1. 内部 Flash 分区状态探测：

   ```text
   coredump
   app_storage
   ```

2. 外部 W25Q64 分区状态探测：

   ```text
   param_store
   modbus_store
   ```

3. 通过 `storage_status` 输出每个分区：

   ```text
   available
   device_ready
   device
   area_id
   offset
   size
   last_error
   ```

4. 支持 `kv` 和 JSON 输出。

可生成测试用例：

1. 正常设备 `internal_flash_ready=yes`。

2. 正常设备 `external_flash_ready=yes`。

3. `param_store.size=131072`。

4. `modbus_store.size=131072`。

5. JSON 输出可被解析。

## 8. CoreDump 诊断服务

当前已实现 `coredump_service`，用于查询、导出和清除 Zephyr CoreDump。

已实现功能：

1. CoreDump 保存在内部 Flash `coredump_partition`。

2. 不主动在开机时上报 CoreDump。

3. 通过 Shell 命令手动查询：

   ```text
   coredump_status
   ```

4. 通过 Shell 命令输出摘要并尝试发布 MQTT：

   ```text
   coredump_report
   ```

5. 通过 Shell 命令导出完整 CoreDump：

   ```text
   coredump_export
   ```

6. CoreDump 导出格式：

   ```text
   #CD:BEGIN#
   #CD:<hex>
   #CD:END#
   ```

7. 通过 Shell 命令清除：

   ```text
   coredump_clear
   ```

8. MQTT 已连接时，CoreDump 摘要和导出也可发布到配置 topic。

9. 当前配置的 CoreDump MQTT topic：

   ```text
   craner/test/log/emb
   ```

可生成测试用例：

1. 无 CoreDump 时状态不误报。

2. 有 CoreDump 时可查询到 found/valid/size。

3. `coredump_report` 输出摘要。

4. `coredump_export` 输出完整分片。

5. `coredump_clear` 清除后再次查询为空。

6. MQTT 未连接时本地输出仍可用。

7. MQTT 已连接时可发布摘要和导出内容。

## 9. RTC 和系统时间服务

当前已实现 `time_service`、`rtc_time_provider` 和 `rtc_trust_store`。

已实现功能：

1. 支持时间源优先级：

   ```text
   GPS > NTP > MANUAL > RTC > BOOT_TICK
   ```

   当前 GPS 仅预留，代码中还没有实际 GPS 时间接入。

2. 支持时间质量：

   ```text
   invalid
   monotonic_only
   estimated
   synced
   high_precision
   ```

3. 上电后尝试从 RTC 恢复系统时间。

4. RTC 必须通过有效性检查，才能作为系统时间源。

5. RTC 无效时记录错误，不作为有效时间源。

6. 网络 ready 后自动进行 NTP 同步。

7. NTP 失败后支持指数退避重试。

8. NTP 成功后定期重新同步。

9. 默认 NTP server：

   ```text
   pool.ntp.org
   ```

10. NTP 或手动校时成功后，根据阈值写回 RTC。

11. 支持自动和手动校正模式：

   ```text
   time_mode auto
   time_mode manual
   ```

12. 支持手动同步：

   ```text
   time_sync
   ```

13. 支持手动设置系统时间和 RTC：

   ```text
   time_set YYYY-MM-DDTHH:MM:SSZ
   rtc_set YYYY-MM-DDTHH:MM:SSZ
   ```

14. 日志时间戳已配置为使用 realtime ISO8601，精度到秒。

可生成测试用例：

1. RTC 无效时不作为系统时间源。

2. RTC 有效时上电后恢复 estimated 时间。

3. DHCP ready 后 NTP 自动同步。

4. NTP 同步后 `time_status` 显示 source=ntp、quality=synced。

5. 手动模式下自动 NTP 不应主动校时。

6. `time_sync` 可手动触发 NTP 同步。

7. `time_set` 和 `rtc_set` 可设置时间。

8. 重启后 RTC 时间仍有效。

9. 日志时间戳使用 UTC ISO8601 秒级格式。

## 10. MQTT 服务

当前已实现共享 MQTT 连接管理器 `mqtt_service_manager`。

已实现功能：

1. 使用单一共享 MQTT client。

2. MQTT broker：

   ```text
   mqtt.craner.hk:1883
   ```

3. 用户名：

   ```text
   hkcrctest
   ```

4. 密码已在 Kconfig 中配置。

5. MQTT Client ID 使用设备身份服务生成。

6. 网络 ready 后自动连接 MQTT。

7. 连接失败后每 5 秒重试。

8. MQTT 连接成功后发布 retained online status。

9. online status topic 格式：

   ```text
   craner/<project>/<type>/<uid>/status/online
   ```

10. online status payload 包含：

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

11. 提供共享 publish、subscribe 和事件分发接口。

可生成测试用例：

1. 网络 ready 后 MQTT 自动连接。

2. Broker 不可用时自动重试。

3. MQTT Client ID 与设备身份一致。

4. online status topic 和 payload 正确。

5. 断网恢复后 MQTT 自动重连。

## 11. MQTT 日志服务

当前已启用 MQTT 日志后端。

已实现功能：

1. Zephyr LOG 同时输出到 UART 和 MQTT。

2. MQTT log topic：

   ```text
   craner/test/log/emb
   ```

3. MQTT log QoS：

   ```text
   QoS 1
   ```

4. MQTT log retain：

   ```text
   false
   ```

5. MQTT log 单条最大消息：

   ```text
   1024 bytes
   ```

6. 日志使用 deferred 模式。

7. 日志线程栈大小：

   ```text
   4096
   ```

8. 日志时间戳使用 realtime ISO8601。

可生成测试用例：

1. MQTT 连接成功后可收到日志。

2. 日志 topic 正确。

3. 日志时间戳为 ISO8601 UTC。

4. MQTT 未连接时系统不崩溃。

5. 大量日志下不再出现 logging 线程栈溢出。

## 12. MQTT 远程诊断命令服务

当前已实现 `mqtt_shell_service`，这是白名单命令服务，不是完整 Shell 透传。

请求 topic：

```text
craner/encoder_hub/cmd/request
```

响应 topic：

```text
craner/encoder_hub/cmd/response
```

当前白名单命令：

```text
fw_time
mqtt_status
net_status
time_status
rtc_status
param_status
param_get <key>
param_set <key> <value>
param_save
param_factory_reset
coredump_status
coredump_report
coredump_export
coredump_clear
time_sync
reboot
```

响应格式：

```json
{"status":"ok","cmd":"xxx","message":"xxx"}
```

或：

```json
{"status":"error","cmd":"xxx","message":"xxx"}
```

可生成测试用例：

1. MQTTX 发布 `mqtt_status` 能收到响应。

2. 白名单命令能执行。

3. 非白名单命令返回 unsupported。

4. `param_get/param_set/param_save` 可远程维护参数。

5. `coredump_report/export/clear` 可远程维护 CoreDump。

6. `reboot` 会触发设备重启。

## 13. Modbus TCP Server

当前已实现 `modbus_tcp_server_app`。

已实现功能：

1. 监听 TCP 端口：

   ```text
   502
   ```

2. Unit ID：

   ```text
   1
   ```

3. 使用 Zephyr Modbus RAW interface 接入项目寄存器服务。

4. 支持固定数量客户端池。

5. 当前最大同时客户端数：

   ```text
   CONFIG_CRANER_MODBUS_TCP_MAX_CLIENTS=4
   ```

6. poll fd 数量为 `max_clients + 1`。

7. 客户端池满时拒绝新连接并关闭 socket。

8. 支持标准 Modbus 回调：

   ```text
   coil_rd
   coil_wr
   input_reg_rd
   holding_reg_rd
   holding_reg_wr
   ```

9. 当前还注册了一个自定义功能码：

   ```text
   function code 101
   ```

可生成测试用例：

1. OpenModScan 可连接 502 端口。

2. Unit ID 1 可通信。

3. 多客户端同时连接最多 4 个。

4. 第 5 个客户端被拒绝或关闭。

5. 读写功能码行为符合寄存器权限。

6. 连接断开后槽位释放，新客户端可连接。

## 14. Modbus 寄存器表和寄存器服务

当前已实现项目级 Modbus 寄存器管理服务。

已实现功能：

1. 寄存器使用显式地址空间，不再自动按数组大小计算。

2. 当前地址空间：

   ```text
   Coil: 0..9
   Input Register: 0..99
   Holding Register: 0..9
   ```

3. 每个寄存器有 flags：

   ```text
   MODBUS_REG_F_READABLE
   MODBUS_REG_F_WRITABLE
   MODBUS_REG_F_PERSISTENT
   ```

4. 地址空间内未定义地址读取返回 `0`。

5. 地址空间内未定义地址写入失败。

6. 地址空间外访问失败。

7. 已定义但无 readable 标志时读取失败。

8. 已定义但无 writable 标志时写入失败。

当前 Coil：

```text
地址 0: REG_SYSTEM_RESET
权限: write-only
持久化: no
```

当前 Input Register：

```text
地址 0..17: 编码器状态、时间戳、错误码、离线状态、圈数和角度值
权限: read-only
持久化: no
```

当前 Holding Register：

```text
地址 0: REG_SYSTEM_RESERVER
权限: read-write
持久化: yes
```

可生成测试用例：

1. 读取未定义 Coil 地址 1 返回 0。

2. 读取 Coil 地址 0 失败，因为 write-only。

3. 写 Coil 地址 0 成功。

4. 读取 Holding 地址 1 返回 0。

5. 写 Holding 地址 1 失败。

6. 读取 Input 地址 18 返回 0。

7. 读取 Input 地址 100 失败。

8. 写 Input 地址 0 失败。

9. 写 Holding 地址 0 成功并可读回。

## 15. Modbus 寄存器持久化

当前已实现 `modbus_register_store`。

已实现功能：

1. 持久化数据保存在外部 W25Q64 的 `modbus-store` 分区。

2. `modbus-store` 分区大小：

   ```text
   128KB
   ```

3. 使用双 bank。

4. 每次保存写 inactive bank。

5. header 和 payload 都使用 CRC32 校验。

6. 使用 sequence 选择最新有效 bank。

7. 启动时自动加载最新有效 bank。

8. 两个 bank 都无效时使用默认寄存器值。

9. 只保存带 `MODBUS_REG_F_PERSISTENT` 的 Coil 和 Holding Register。

10. 当前实际只有 Holding Register 地址 `0` 会持久化。

11. 使用地址型 TLV 记录保存，便于后续新增/删除寄存器。

12. 写持久化寄存器后标记 dirty。

13. 支持延迟保存，默认延迟：

   ```text
   3000 ms
   ```

14. 支持手动保存：

   ```text
   modbus_store_save
   ```

15. 支持手动重新加载：

   ```text
   modbus_store_load
   ```

16. 支持清除：

   ```text
   modbus_store_clear
   ```

17. 支持状态查询：

   ```text
   modbus_store_status
   ```

可生成测试用例：

1. 空分区启动不报错。

2. 写 Holding 地址 0 后 dirty=yes。

3. 延迟后 dirty=no，save_count 增加。

4. 重启后 Holding 地址 0 恢复保存值。

5. 手动保存立即生效。

6. 清除后恢复默认值。

7. 连续快速写入合并保存。

8. 保存过程中断电不会加载随机值。

9. sequence 递增且 bank 切换。

## 16. Modbus RTU 编码器采集

当前已实现 `modbus_rtu_client_app`，用于读取 3 路编码器。

已实现功能：

1. 支持 3 个编码器线程：

   ```text
   slewing
   luffing
   hoisting
   ```

2. 每个编码器使用 Modbus RTU client。

3. 默认 Unit ID：

   ```text
   1
   ```

4. 默认波特率：

   ```text
   9600
   ```

5. 默认读取 Holding Register 起始地址：

   ```text
   0x0002
   ```

6. 默认读取寄存器数量：

   ```text
   2
   ```

7. 默认轮询周期：

   ```text
   50 ms
   ```

8. 采集结果写入项目 Input Register 表。

9. 通信错误会写入错误码和离线状态。

10. 成功读取会更新时间戳、错误码、离线状态和编码器值。

可生成测试用例：

1. 三路 RS485 编码器在线时 Input Register 更新。

2. 编码器断开时离线状态更新。

3. 错误码能反映通信失败类型。

4. 轮询期间系统不崩溃。

5. Modbus TCP 可读取 RTU 采集到的 Input Register。

## 17. MCUboot OTA 控制

当前已启用 MCUboot image manager 和 OTA 控制模块。

已实现功能：

1. 使用 MCUboot swap/scratch 架构。

2. 启用 image manager：

   ```text
   CONFIG_IMG_MANAGER=y
   CONFIG_MCUBOOT_IMG_MANAGER=y
   ```

3. 生成 confirmed image。

4. 启用 mcumgr UDP transport。

5. UDP 端口：

   ```text
   1337
   ```

6. 编译了 `ota_control_app.c`，用于 Shell 控制 MCUboot OTA 状态机。

可生成测试用例：

1. 查询当前镜像状态。

2. 上传测试镜像。

3. 测试镜像启动后确认。

4. 未确认测试镜像自动回滚。

5. OTA 过程中断电恢复。

注意：具体 Shell 命令需要结合 `ota_control_app.c` 再生成详细测试用例。

## 18. 系统健康监控和看门狗

当前已启用 `system_health_app`。

已实现功能：

1. 启用硬件 watchdog。

2. 系统健康线程默认启用。

3. 健康检查周期：

   ```text
   100 ms
   ```

4. Watchdog timeout：

   ```text
   30000 ms
   ```

5. 系统健康事件表已编译。

6. 状态 LED、业务事件监控和喂狗逻辑在系统健康模块中实现。

7. 可选 UDP syslog probe 当前默认未启用。

可生成测试用例：

1. 正常运行时设备不被 watchdog 复位。

2. 健康状态异常时看门狗行为符合设计。

3. 状态 LED 指示符合健康状态。

4. 业务事件超时能被健康模块识别。

注意：具体事件定义和 LED 行为需要结合系统健康需求文档进一步展开。

## 19. 日志和诊断

当前日志配置和诊断功能包括：

1. `printk` 保留在板卡 console UART。

2. Zephyr LOG 后端为 UART 和 MQTT。

3. Shell 支持串口和 Telnet。

4. LOG 支持运行时过滤。

5. 支持 log shell command。

6. 日志使用 rate limit。

7. 日志时间戳使用 realtime ISO8601 UTC。

8. CoreDump 支持内部 Flash 保存和手动导出。

9. MQTT remote shell 支持白名单诊断命令。

可生成测试用例：

1. Telnet Shell 可用。

2. MQTTX 可收到 MQTT log。

3. log timestamp 正确。

4. CoreDump 查询和导出可用。

5. 远程诊断命令只允许白名单。

## 20. 当前已知限制和测试注意事项

1. 当前 hostname 默认规则为：

   ```text
   craner-project-type-<name_uid>
   ```

   测试组可通过 `device/project` 和 `device/type` 修改 project/type，执行 `param_save` 后重启生效。

2. 当前 hostname 由 `company + project + type + name_uid` 生成，不再提供独立的 `device/hostname` 参数；`name_uid` 是 5 字节 short UID 的最后 2 字节。

3. GPS 时间源仅预留优先级，当前未实现实际 GPS 接入。

4. 当前 Modbus 持久化实际只有 Holding Register 地址 `0`。

5. 当前 MQTT shell 不包含 `modbus_store_status` 和 `modbus_store_clear` 白名单命令。

6. 当前 `fault_oops` 在 `prj.conf` 中启用，适合测试 CoreDump；正式量产固件应关闭。

7. 文档中涉及 MQTT 密码，不建议公开给非授权测试环境。

8. 正式 Modbus 测试前，开发人员仍应提供完整寄存器表。

9. 外部 W25Q64 异常时，设备参数和 Modbus 持久化能力会受影响；CoreDump 设计上保存在内部 Flash。

10. 当前 OTA 具体 Shell 命令需要进一步从 `ota_control_app.c` 整理后再生成完整测试用例。

## 21. 建议拆分生成的测试文档

建议按以下文档拆分生成测试用例：

1. `persistent_storage_service_test_cases.md`

   覆盖存储分区、设备参数、Shell 输出格式、CoreDump、本地和 MQTTX 维护。

2. `modbus_tcp_test_cases.md`

   覆盖 Modbus TCP 连接、寄存器读写、地址边界、权限、多客户端，以及 Modbus 寄存器持久化。

3. `network_identity_test_cases.md`

   覆盖设备身份、MAC、hostname、mDNS、DHCP、网络恢复。

4. `time_rtc_ntp_test_cases.md`

   覆盖 RTC 有效性、NTP 自动同步、手动校时、时间源优先级、日志时间戳。

5. `mqtt_diagnostics_test_cases.md`

   覆盖 MQTT 连接、online status、MQTT log、MQTT shell 白名单命令。

6. `encoder_rtu_test_cases.md`

   覆盖 3 路编码器 RTU 采集、错误码、离线状态、Input Register 更新。

7. `ota_test_cases.md`

   覆盖 MCUboot OTA 上传、测试、确认、回滚和断电恢复。

8. `system_health_test_cases.md`

   覆盖 watchdog、状态 LED、业务事件健康监控。
