# 网络功能与性能测试用例

本文档面向测试组，用于对已经烧录好固件、已经封盒的整机进行通用网络能力验收。测试组不需要构建固件、烧录固件、打开外壳、连接调试器或使用串口，所有测试均通过以太网完成。

本文档只覆盖基础网络能力：以太网链路、DHCP、mDNS、Telnet Shell、NTP、断网恢复、重启恢复、网络统计和基础连通性能。Modbus TCP 测试见 [modbus_tcp_test_cases.md](modbus_tcp_test_cases.md)，MQTT 测试见 [mqtt_function_test_cases.md](mqtt_function_test_cases.md)。

## 1. 测试范围

1. 以太网链路：验证设备接入有线网络后可建立 100M full-duplex 链路，并能正常收发 IPv4 数据。
2. DHCP：验证设备可通过 DHCP 获取 IP、网关、子网掩码、DHCP server、租约和续约时间。
3. mDNS：验证 PC 可通过 `.local` 主机名访问设备。
4. Telnet Shell：验证 MobaXterm 可通过 Telnet 访问设备 Shell，并执行网络状态命令。
5. NTP：验证设备网络 ready 后可进行 NTP/SNTP 时间同步，异常时可稳定重试。
6. 网络恢复：验证拔插网线、DHCP 服务异常恢复后设备可重新进入 ready 状态。
7. 网络性能和稳定性：验证 ping 延迟和丢包、Telnet 长时间连接、重启后网络恢复和网络统计。

## 2. 结果状态定义

1. `通过`：实际结果与预期结果一致。
2. `失败`：设备响应、网络状态、协议返回、恢复时间、丢包率或性能结果与预期不一致；必须记录缺陷编号。
3. `阻塞`：因网络环境、DHCP server、交换机、PC 防火墙或供电条件等外部原因导致无法执行。
4. `不适用`：当前测试环境或固件缺少对应条件，例如没有可控 DHCP 环境，或未提供 zperf 专用测试固件。

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

   其中 `{project}` 默认是 `project`，`{type}` 默认是 `type`，`name_uid` 是设备短 UID 的最后 2 字节，例如 `9460`。测试前应由开发人员提供实际主机名；测试报告中必须记录实际主机名和解析到的 IP 地址。

3. 开发人员必须提供的信息：

   ```text
   设备 mDNS 主机名:
   固件版本:
   本轮测试是否允许断网:
   本轮测试是否允许重启:
   本轮测试是否允许断电:
   是否提供 zperf 专用测试固件:
   ```

4. 测试工具：

   ```text
   MobaXterm: Telnet Shell
   Windows PowerShell: ping、Test-NetConnection
   可选 Wireshark: 抓包分析
   可选交换机管理界面: 查看端口速率、双工、错误包和丢包统计
   ```

5. 推荐网络拓扑：

   ```text
   PC ---- 交换机 ---- 被测设备
              |
              +---- DHCP server / 网关 / NTP server
   ```

   如果 PC 和设备不在同一二层网络，需要确认路由、防火墙和 mDNS 组播策略。

## 4. 当前网络配置摘要

当前固件启用的主要网络功能：

```text
CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_ETH_STM32_HAL=y
CONFIG_PHY_GENERIC_MII=y
CONFIG_NET_IPV4=y
CONFIG_NET_IPV6=n
CONFIG_NET_ARP=y
CONFIG_NET_UDP=y
CONFIG_NET_TCP=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_DHCPV4=y
CONFIG_NET_HOSTNAME_ENABLE=y
CONFIG_NET_HOSTNAME_DYNAMIC=y
CONFIG_MDNS_RESPONDER=y
CONFIG_SHELL_BACKEND_TELNET=y
CONFIG_SNTP=y
```

当前主要网络资源配置：

```text
CONFIG_NET_PKT_RX_COUNT=16
CONFIG_NET_PKT_TX_COUNT=16
CONFIG_NET_BUF_RX_COUNT=32
CONFIG_NET_BUF_TX_COUNT=32
CONFIG_NET_BUF_DATA_SIZE=1536
CONFIG_NET_MAX_CONTEXTS=16
CONFIG_NET_MAX_CONN=16
CONFIG_ZVFS_POLL_MAX=8
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096
CONFIG_NET_MGMT_EVENT_STACK_SIZE=4096
CONFIG_NET_TX_STACK_SIZE=4096
CONFIG_NET_RX_STACK_SIZE=4096
CONFIG_NET_TCP_WORKQ_STACK_SIZE=4096
CONFIG_SHELL_STACK_SIZE=4096
```

当前本文档涉及的网络服务：

```text
Telnet Shell: TCP 23
mcumgr UDP: UDP 1337
NTP/SNTP client: pool.ntp.org
mDNS responder: UDP 5353
```

## 5. 验收总表

测试组先根据下表进行总体验收记录，再按后续 NET-xx 用例执行详细测试。`验收结果` 一栏由测试组填写，只能填写 `通过`、`失败`、`阻塞` 或 `不适用`；失败时填写缺陷编号。

| 编号 | 测试内容 | 验收标准 | 验收结果 |
| --- | --- | --- | --- |
| NET-01 | 测试资料输入检查 | 开发人员已提供主机名、固件版本，以及是否允许断网/重启/断电。 |  |
| NET-02 | mDNS 主机名解析 | PC 可通过 `craner-{project}-{type}-{name_uid}.local` 解析到设备 IP。 |  |
| NET-03 | DHCP 获取地址 | `net_status` 显示 `ready=yes`，IP、netmask、gateway、dhcp_server、lease_s 有效。 |  |
| NET-04 | 基础 ping 连通性 | PC 连续 ping 设备 100 次，丢包率为 0%。 |  |
| NET-05 | Telnet Shell 连接 | MobaXterm 可连接 TCP 23 并执行 `net_status`。 |  |
| NET-06 | NTP 自动同步 | 网络 ready 后 `time_status` 最终显示 NTP 同步成功，或在 NTP 不可达时稳定重试。 |  |
| NET-07 | 网线拔出检测 | 拔出网线后设备不可 ping，Telnet 连接断开或不可用。 |  |
| NET-08 | 网线恢复检测 | 插回网线后设备重新 DHCP，`net_status` 恢复 `ready=yes`。 |  |
| NET-09 | DHCP 异常恢复 | DHCP 不可用时设备进入等待/失败状态，恢复 DHCP 后能重新获取地址。 |  |
| NET-10 | mDNS 恢复 | 断网恢复后 `.local` 主机名可再次解析并 ping 通。 |  |
| NET-11 | Telnet 稳定性 | Telnet 会话保持 30 分钟可用，状态命令响应正常。 |  |
| NET-12 | 并发基础网络压力 | Telnet 和持续 ping 同时运行 30 分钟，设备不崩溃。 |  |
| NET-13 | ping 长稳丢包 | PC 连续 ping 设备 30 分钟，丢包率不超过验收阈值。 |  |
| NET-14 | 重启后网络恢复 | 执行 `reboot` 或整机重新上电后，设备能自动恢复 DHCP、mDNS、Telnet。 |  |
| NET-15 | 网络统计检查 | `net stats` 或 `net iface` 未出现持续增长且不可解释的 drop/error。 |  |
| NET-16 | 可选 zperf 吞吐专项 | 仅在开发提供 zperf 专用固件时执行，UDP/TCP 吞吐和丢包满足专项标准。 |  |

## 6. 详细测试用例

### NET-01 测试资料输入检查

1. 测试目的：
   确认网络测试开始前，测试组已经获得执行测试所需的全部信息。

2. 前置条件：
   被测设备已经上电，网线已连接到测试网络。

3. 测试步骤：
   检查开发人员是否已提供以下信息：

   ```text
   设备 mDNS 主机名
   固件版本
   是否允许断网
   是否允许重启
   是否允许断电
   是否提供 zperf 专用测试固件
   ```

4. 预期结果：
   所有输入信息完整，且与被测固件版本一致。

5. 判定标准：
   信息完整判定通过；缺少关键信息时，相关测试记录为阻塞。

### NET-02 mDNS 主机名解析

1. 测试目的：
   验证 PC 可通过设备 `.local` 主机名发现设备。

2. 前置条件：
   PC 和设备在同一二层网络，网络允许 mDNS 组播 `224.0.0.251:5353`。

3. 测试步骤：
   在 PC PowerShell 执行：

   ```powershell
   ping craner-{project}-{type}-{name_uid}.local
   ```

   记录解析到的 IP 地址。

4. 预期结果：
   主机名可解析为 IPv4 地址，并能收到 ICMP 回复。

5. 判定标准：
   `.local` 可解析且 ping 通判定通过。如果 ping IP 可以通但 `.local` 不通，记录为 mDNS 或 PC 网络策略问题。

### NET-03 DHCP 获取地址

1. 测试目的：
   验证设备通过 DHCP 获取完整 IPv4 网络参数。

2. 前置条件：
   设备所在网络存在可用 DHCP server。

3. 测试步骤：
   使用 MobaXterm 新建 Telnet Session：

   ```text
   Remote host: craner-{project}-{type}-{name_uid}.local
   Port: 23
   Terminal: Telnet
   ```

   连接成功后执行：

   ```text
   net_status
   net iface
   ```

4. 预期结果：

   ```text
   state: ready
   link_up: yes
   ready: yes
   ip: <有效 IPv4>
   netmask: <有效 netmask>
   gateway: <有效 gateway>
   dhcp_server: <有效 DHCP server>
   lease_s: >0
   renew_s: >0
   ```

5. 判定标准：
   DHCP 参数完整且 `ready=yes` 判定通过。

### NET-04 基础 ping 连通性

1. 测试目的：
   验证设备在局域网内基础 IP 连通性稳定。

2. 前置条件：
   NET-02 和 NET-03 已通过。

3. 测试步骤：
   在 PC PowerShell 执行：

   ```powershell
   ping craner-{project}-{type}-{name_uid}.local -n 100
   ```

4. 预期结果：
   100 个 ping 包全部收到回复，丢包率为 0%。

5. 判定标准：
   丢包率 0% 判定通过；若有丢包，记录最小、最大、平均延迟、丢包数量和网络拓扑。

### NET-05 Telnet Shell 连接

1. 测试目的：
   验证 Telnet Shell 可作为封盒整机网络维护入口。

2. 前置条件：
   设备已经通过 DHCP 获取 IP，PC 可访问设备。

3. 测试步骤：
   使用 MobaXterm 新建 Telnet Session：

   ```text
   Remote host: craner-{project}-{type}-{name_uid}.local
   Port: 23
   Terminal: Telnet
   ```

   连接成功后执行：

   ```text
   fw_time
   net_status
   storage_status
   ```

4. 预期结果：
   MobaXterm 可进入 Shell，命令有响应，无明显卡死。

5. 判定标准：
   Telnet 可连接，基础命令可执行判定通过。

### NET-06 NTP 自动同步

1. 测试目的：
   验证网络 ready 后 NTP/SNTP 时间同步可工作。

2. 前置条件：
   测试网络允许访问 NTP server，默认 NTP server 为 `pool.ntp.org`。

3. 测试步骤：
   网络 ready 后等待最多 60 秒，在 Telnet Shell 执行：

   ```text
   time_status
   ```

4. 预期结果：
   成功场景显示：

   ```text
   valid: yes
   source: ntp
   quality: synced
   ```

   如果当前网络无法访问 NTP server，应显示可解释的 `fail_count` 和 `retry_delay_ms`，设备不应崩溃。

5. 判定标准：
   可访问公网 NTP 时必须同步成功；不可访问时必须稳定重试。

### NET-07 网线拔出检测

1. 测试目的：
   验证以太网链路断开后设备状态和通用网络连接表现正确。

2. 前置条件：
   本轮测试允许断网。设备已处于 ready 状态。

3. 测试步骤：
   拔出设备网线，等待 5 到 10 秒；在 PC 上尝试 ping 设备，并观察已有 Telnet 连接。

4. 预期结果：
   PC ping 不通设备。已建立的 Telnet 连接断开或失效。

5. 判定标准：
   断网后设备不应继续被误判为网络可用。恢复网线后执行 NET-08。

### NET-08 网线恢复检测

1. 测试目的：
   验证网线重新插入后设备可恢复 DHCP 和通用网络服务。

2. 前置条件：
   已执行 NET-07。

3. 测试步骤：
   插回网线，周期性执行：

   ```powershell
   ping craner-{project}-{type}-{name_uid}.local
   ```

   Telnet 恢复后执行：

   ```text
   net_status
   ```

4. 预期结果：
   设备重新获取 IP，`net_status` 恢复：

   ```text
   link_up: yes
   ready: yes
   ```

5. 判定标准：
   设备可自动恢复网络。若项目尚未定义恢复时间阈值，测试报告记录实测恢复时间。

### NET-09 DHCP 异常恢复

1. 测试目的：
   验证 DHCP 不可用时设备不会崩溃，并在 DHCP 恢复后重新获取地址。

2. 前置条件：
   测试环境允许临时关闭 DHCP，或允许将设备接入无 DHCP 网络。

3. 测试步骤：
   让设备启动或恢复网络时无法获取 DHCP，观察 `net_status`；恢复 DHCP 后继续观察。

4. 预期结果：
   DHCP 不可用时设备进入 `dhcp_waiting` 或 `failed`，`fail_count` 可增加，`next_retry_ms` 按指数退避增加。DHCP 恢复后设备进入 `ready`。

5. 判定标准：
   DHCP 异常不会导致设备崩溃，恢复 DHCP 后能重新 ready。

### NET-10 mDNS 恢复

1. 测试目的：
   验证断网恢复后 `.local` 主机名仍可使用。

2. 前置条件：
   NET-07 和 NET-08 已执行。

3. 测试步骤：
   在 PC PowerShell 执行：

   ```powershell
   ping craner-{project}-{type}-{name_uid}.local -n 20
   ```

4. 预期结果：
   主机名可解析并 ping 通。

5. 判定标准：
   mDNS 恢复正常。如果 IP 可 ping 通但 `.local` 失败，记录为 mDNS 恢复异常或 PC 环境限制。

### NET-11 Telnet 稳定性

1. 测试目的：
   验证 Telnet Shell 长时间连接稳定。

2. 前置条件：
   设备网络 ready，MobaXterm Telnet 已连接。

3. 测试步骤：
   Telnet 会话保持 30 分钟，每 5 分钟执行：

   ```text
   net_status
   time_status
   storage_status
   ```

4. 预期结果：
   Telnet 会话不断开，或断开后可重新连接；命令响应正常。

5. 判定标准：
   30 分钟内维护入口可用判定通过。

### NET-12 并发基础网络压力

1. 测试目的：
   验证 Telnet 和 ping 同时运行时通用网络服务稳定。

2. 前置条件：
   NET-05 已通过。

3. 测试步骤：
   同时执行：

   ```text
   1 个 MobaXterm Telnet 会话，每分钟执行 net_status
   PC 持续 ping 设备
   ```

   持续 30 分钟。

4. 预期结果：
   设备不崩溃，Telnet 可用，ping 丢包率不超过验收阈值。

5. 判定标准：
   并发通用网络服务运行 30 分钟稳定判定通过。

### NET-13 ping 长稳丢包

1. 测试目的：
   验证设备在长时间基础网络通信下的丢包情况。

2. 前置条件：
   设备处于网络 ready 状态。

3. 测试步骤：
   在 PC PowerShell 执行：

   ```powershell
   ping craner-{project}-{type}-{name_uid}.local -t
   ```

   持续 30 分钟后停止，记录统计信息。

4. 预期结果：
   长时间 ping 无明显连续丢包。

5. 判定标准：
   如果项目未定义阈值，建议以 30 分钟丢包率 `<= 0.1%` 作为初始验收参考；正式阈值由项目确认。

### NET-14 重启后网络恢复

1. 测试目的：
   验证设备重启后网络服务可自动恢复。

2. 前置条件：
   本轮测试允许执行 `reboot` 或整机重新上电。

3. 测试步骤：
   Telnet Shell 执行：

   ```text
   reboot
   ```

   设备重启后周期性 ping 主机名，随后重新连接 Telnet。

4. 预期结果：
   设备自动获取 DHCP，mDNS 可解析，Telnet 可连接。

5. 判定标准：
   重启后通用网络服务恢复判定通过。若项目尚未定义恢复时间阈值，记录实测恢复时间。

### NET-15 网络统计检查

1. 测试目的：
   检查网络运行后是否出现异常 drop、error 或资源耗尽迹象。

2. 前置条件：
   已完成基础和并发网络测试。

3. 测试步骤：
   在基础测试前后分别执行：

   ```text
   net stats
   net iface
   ```

   如果 Shell 支持，可在测试前执行：

   ```text
   net stats reset
   ```

4. 预期结果：
   测试过程中不应出现持续增长且不可解释的 drop、processing error 或连接异常。

5. 判定标准：
   无异常统计，或异常有明确外部原因，判定通过。

### NET-16 可选 zperf 吞吐专项

1. 测试目的：
   在开发提供 zperf 专用固件时，验证纯 UDP/TCP 吞吐能力。

2. 前置条件：
   当前正式固件默认不执行本用例。仅在开发提供 zperf 专用测试固件时执行，否则记录为 `不适用`。

3. 测试步骤：
   PC 安装 iPerf 2。参考项目文档：

   ```text
   docs/zephyr_zperf_network_test_guide.md
   ```

   执行 UDP/TCP upload/download 测试，并记录 throughput、jitter、丢包率和 `net stats`。

4. 预期结果：
   UDP/TCP 吞吐满足专项测试标准，且无明显 buffer allocation failed、net_pkt drop 或 TCP 异常。

5. 判定标准：
   以 zperf 专项测试标准为准。正式固件不执行该项。

## 7. 性能指标建议

如果项目暂未定义网络性能阈值，可先使用以下建议作为测试记录参考，后续由项目正式确认：

| 指标 | 建议初始验收参考 |
| --- | --- |
| 局域网 ping 100 次丢包率 | 0% |
| 局域网 ping 30 分钟丢包率 | <= 0.1% |
| Telnet Shell 命令响应 | 人工操作无明显卡顿 |
| 断网恢复 | 记录实测恢复时间，后续固化阈值 |
| 重启后网络恢复 | 记录实测恢复时间，后续固化阈值 |

## 8. 缺陷记录要求

1. mDNS 失败时，记录 IP 是否可直接 ping 通。如果 IP 可通但 `.local` 不通，附上 Windows 网络环境、是否跨网段、是否禁用 mDNS。
2. DHCP 失败时，记录 `net_status`、`net iface`、DHCP server 状态和交换机端口状态。
3. ping 丢包时，记录 ping 统计、是否并发运行 Telnet、交换机端口统计和网线/端口信息。
4. Telnet 断开时，记录 MobaXterm 输出、断开时间点、设备是否仍可 ping。
5. 断网恢复失败时，记录断网方式、断网持续时间、恢复后等待时间和 `net_status` 输出。
6. 任何疑似网络资源不足问题，都记录 Telnet 是否同时运行，以及 `net stats` 输出。
