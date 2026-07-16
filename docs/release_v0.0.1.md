# craner_encoder_hub v0.0.1 发布说明与测试用例

本文档面向 v0.0.1 首个发布版本，用于说明当前固件的基础能力、业务功能、调试诊断方式，并给测试组提供可执行的测试用例。

## 1. 版本范围

| 项目 | 内容 |
| --- | --- |
| 产品/应用 | `craner_encoder_hub` |
| 发布版本 | `v0.0.1` |
| 目标板卡 | `craner_general_stm32h743vit6` |
| MCU | STM32H743VIT6 |
| PCB | Craner General STM32H743VIT6 PCB V1.1.0 |
| Zephyr 板级目录 | `boards/craner/craner_general_stm32h743vit6/` |
| 默认 IPv4 | `192.168.18.32` |
| 默认网关 | `192.168.18.1` |
| 默认子网掩码 | `255.255.255.0` |
| Modbus TCP 端口 | `502` |
| Telnet Shell 端口 | `23` |
| mcumgr UDP 端口 | `1337` |
| 控制台串口 | UART5，PB6 TX / PB5 RX，115200 8N1 |

发布前检查项：当前仓库 `VERSION` 文件内容为 `1.2.6`，如果发布包需要严格体现 `v0.0.1`，请在出包前统一版本号来源、制品命名和发布标签。

## 2. 功能特性概述

v0.0.1 是 `craner_encoder_hub` 的首个可发布版本，目标是完成编码器数据采集、上位机 Modbus TCP 访问、基础网络调试、现场诊断和 OTA 升级闭环。该版本以稳定运行和可测试交付为主，功能边界清晰，适合作为后续业务扩展的基线版本。主要功能特性如下：

1. 板级启动与基础硬件初始化：固件支持 `craner_general_stm32h743vit6` 自定义板卡启动，面向 STM32H743VIT6 和 Craner General STM32H743VIT6 PCB V1.1.0 硬件平台，启动后自动完成基础外设初始化，并使能 3.3V/CCTV、5V、网络桥接等业务电源控制 GPIO。

2. 串口控制台、Shell 与日志诊断：固件通过 UART5 提供控制台、`printk` 和标准输出能力，同时启用串口 Shell 与 Telnet Shell，支持固件编译时间查询、编码器采集统计查看与清零、OTA 状态控制、Zephyr 网络诊断和日志诊断等现场维护能力。

3. 以太网与 TCP/IP 通信：固件启用 STM32 RMII Ethernet、静态 IPv4、ARP、UDP、TCP、Socket、网络管理事件和网络统计功能，默认 IP 为 `192.168.18.32`，用于承载 Telnet Shell、Modbus TCP Server 和 mcumgr UDP OTA 通信。

4. 三路 Modbus RTU 编码器采集：固件作为 Modbus RTU Client，通过 UART7、UART8、UART4 分别轮询回转、变幅、起升三路编码器，默认通信参数为 9600 8E1、Unit ID 1、50 ms 轮询周期，并将采集到的业务数据、时间戳、错误码和离线状态写入内部寄存器表。

5. Modbus TCP Server 上位机接口：固件监听 TCP `502` 端口，作为 Modbus TCP Server 向上位机开放统一寄存器访问能力，支持读取三路编码器 Input Register，读写系统 Holding Register，并访问系统控制 Coil。

6. 统一寄存器管理：固件内置 Coil、Input Register、Holding Register 三类寄存器表，并通过寄存器服务统一管理默认值、读写访问、连续地址访问和线程互斥，保证 RTU 采集线程与 TCP Server 访问同一份业务数据。

7. 系统健康、状态 LED 与看门狗：固件提供系统健康线程，用于检测编码器采集事件是否离线，驱动状态 LED 显示正常或故障模式，并在系统健康条件满足时喂硬件看门狗，提升现场长期运行可靠性。

8. MCUboot OTA 升级：固件采用 sysbuild 同时构建 MCUboot 与应用，支持 MCUboot swap scratch 升级模式、mcumgr UDP 镜像传输和 Shell OTA 控制命令，可执行 test、permanent、confirm、erase secondary 和 reboot 等升级维护动作。

9. 测试与交付支撑：文档提供构建烧录、启动外设、Shell、网络、Modbus TCP、Modbus RTU、系统健康、OTA 和鲁棒性测试用例，测试组可按用例完成首发版本验收和回归测试。

核心业务数据流：

```text
三路 Modbus RTU 编码器
        -> RTU Client 周期读取
        -> 内部寄存器服务更新 Input Register
        -> Modbus TCP Server 暴露给上位机
        -> Shell/日志/健康线程提供现场诊断
```

对测试组而言，v0.0.1 的关键验收对象是：设备可启动、三路编码器数据可采集、上位机可通过 Modbus TCP 读取寄存器、Telnet/串口 Shell 可诊断、OTA 流程可验证、异常断线后系统不崩溃。

## 3. 基础功能

### 3.1 系统启动

固件启动后进入常驻主循环，控制台输出启动信息。应用入口保持较薄，业务能力由独立模块通过 Zephyr 线程或 `SYS_INIT` 初始化。

启动后预期可观察到：

```text
craner_encoder_hub started on craner_general_stm32h743vit6
printk remains routed to the board console UART
Shell backend is Telnet on port 23 after Ethernet is up
```

### 3.2 电源控制

系统启动早期自动使能板载业务电源控制 GPIO：

| 电源项 | Devicetree alias | GPIO | 默认状态 |
| --- | --- | --- | --- |
| 3.3V 与 CCTV 电源 | `power-3v3-and-cctv` | PE6 | 上电使能 |
| 5V 电源 | `power-5v` | PC13 | 上电使能 |
| 网络桥接电源 | `power-net-brigde` | PB3 | 上电使能 |

如果 GPIO 控制器未就绪或配置失败，模块会通过 Zephyr 日志输出错误。

### 3.3 控制台、Shell 与日志

v0.0.1 默认启用：

| 功能 | 配置 |
| --- | --- |
| `printk`/标准输出 | UART5 控制台 |
| Shell | 串口 Shell + Telnet Shell |
| 日志系统 | Zephyr LOG |
| 日志后端 | UART |
| 日志运行时过滤 | 支持 |
| Shell 日志命令 | 支持 |

可用 Shell 命令包括：

| 命令 | 说明 |
| --- | --- |
| `fw_time` | 查看固件编译日期和时间 |
| `show_encoder_stats` | 查看 3 路 Modbus RTU 编码器读取统计 |
| `clear_encoder_stats` | 清空编码器读取统计 |
| `ota show` | 查看 MCUboot OTA 状态 |
| `ota test` | 将 slot1 镜像标记为 test 升级 |
| `ota permanent` | 将 slot1 镜像标记为 permanent 升级 |
| `ota confirm` | 确认当前镜像为稳定镜像 |
| `ota erase-secondary` | 擦除 slot1 镜像 |
| `ota reboot` | 重启 MCU |

### 3.4 网络基础能力

固件启用 STM32 以太网、IPv4、ARP、UDP、TCP、Socket、网络管理事件、网络 Shell 和网络统计。默认不启用 DHCP，使用静态 IPv4：

```text
IP:      192.168.18.32
Netmask: 255.255.255.0
Gateway: 192.168.18.1
```

测试 PC 建议配置在同一网段，例如 `192.168.18.4/24`。

### 3.5 MCUboot 与 OTA 基础能力

v0.0.1 使用 sysbuild 同时构建 MCUboot 和应用，采用 MCUboot swap scratch 模式：

| 分区 | 地址 | 大小 | 说明 |
| --- | --- | --- | --- |
| `mcuboot` | `0x00000000` | `0x00020000` | Bootloader |
| `image-0` | `0x00020000` | `0x000c0000` | 主应用 slot0 |
| `image-1` | `0x000e0000` | `0x000c0000` | OTA 下载 slot1 |
| `image-scratch` | `0x001a0000` | `0x00020000` | swap scratch |
| `app-storage` | `0x001c0000` | `0x00040000` | 应用存储 |

构建脚本默认生成 OTA 镜像到：

```text
build\craner_general_stm32h743vit6\ota_images\app_update_signed.bin
build\craner_general_stm32h743vit6\ota_images\app_initial_confirmed.hex
```

## 4. 业务功能

### 4.1 三路 Modbus RTU 编码器采集

固件作为 Modbus RTU Client，分别轮询三路编码器：

| 编码器 | Devicetree alias | UART | 引脚 | 串口参数 |
| --- | --- | --- | --- | --- |
| 回转编码器 | `modbus-slewing-encoder` | UART7 | PE8 TX / PE7 RX | 9600 8E1 |
| 变幅编码器 | `modbus-luffing-encoder` | UART8 | PE1 TX / PE0 RX | 9600 8E1 |
| 起升编码器 | `modbus-hook-encoder` | UART4 | PD1 TX / PD0 RX | 9600 8E1 |

每路编码器默认参数：

| 参数 | 值 |
| --- | --- |
| Unit ID | `1` |
| 功能码 | FC03 Read Holding Registers |
| 起始地址 | `0x0002` |
| 数量 | `2` |
| 轮询周期 | `50 ms` |
| 接收超时 | `200000 us` |

采集成功时，固件将两个寄存器值写入内部 Input Register，同时记录本机毫秒时间戳、错误码和在线状态。采集失败时，固件写入错误码并将对应离线状态置为 `1`。

### 4.2 Modbus TCP Server

固件作为 Modbus TCP Server 对上位机开放数据读取，监听 TCP `502` 端口，Unit ID 为 `1`。服务通过内部寄存器服务访问统一寄存器表。

支持的寄存器类型：

| 类型 | 数量 | 用途 |
| --- | --- | --- |
| Coil | 1 | 系统控制位 |
| Input Register | 18 | 三路编码器采集数据 |
| Holding Register | 1 | 系统可写寄存器 |

Coil 表：

| 地址 | 名称 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `0x0000` | `REG_SYSTEM_RESET` | `0` | 系统复位控制预留位 |

Holding Register 表：

| 地址 | 名称 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `0x0000` | `REG_SYSTEM_RESERVER` | `0` | 系统时间戳/业务写入预留 |

Input Register 表：

| 地址 | 名称 | 说明 |
| --- | --- | --- |
| `0x0000` | `REG_SLEWING_TIMESTAMP_H` | 回转编码器更新时间高 16 位 |
| `0x0001` | `REG_SLEWING_TIMESTAMP_L` | 回转编码器更新时间低 16 位 |
| `0x0002` | `REG_SLEWING_ERROR_CODE` | 回转编码器最近一次错误码 |
| `0x0003` | `REG_SLEWING_OFFLINE_STATUS` | 回转编码器离线状态，`0` 在线，`1` 离线 |
| `0x0004` | `REG_SLEWING_TRUN_CNT` | 回转编码器第 1 个业务寄存器值 |
| `0x0005` | `REG_SLEWING_SINAGLE_VAL` | 回转编码器第 2 个业务寄存器值 |
| `0x0006` | `REG_LUFFING_TIMESTAMP_H` | 变幅编码器更新时间高 16 位 |
| `0x0007` | `REG_LUFFING_TIMESTAMP_L` | 变幅编码器更新时间低 16 位 |
| `0x0008` | `REG_LUFFING_ERROR_CODE` | 变幅编码器最近一次错误码 |
| `0x0009` | `REG_LUFFING_OFFLINE_STATUS` | 变幅编码器离线状态，`0` 在线，`1` 离线 |
| `0x000A` | `REG_LUFFING_TRUN_CNT` | 变幅编码器第 1 个业务寄存器值 |
| `0x000B` | `REG_LUFFING_SINAGLE_VAL` | 变幅编码器第 2 个业务寄存器值 |
| `0x000C` | `REG_HOISTING_TIMESTAMP_H` | 起升编码器更新时间高 16 位 |
| `0x000D` | `REG_HOISTING_TIMESTAMP_L` | 起升编码器更新时间低 16 位 |
| `0x000E` | `REG_HOISTING_ERROR_CODE` | 起升编码器最近一次错误码 |
| `0x000F` | `REG_HOISTING_OFFLINE_STATUS` | 起升编码器离线状态，`0` 在线，`1` 离线 |
| `0x0010` | `REG_HOISTING_TRUN_CNT` | 起升编码器第 1 个业务寄存器值 |
| `0x0011` | `REG_HOISTING_SINAGLE_VAL` | 起升编码器第 2 个业务寄存器值 |

### 4.3 系统健康与看门狗

系统健康线程负责状态 LED、业务事件离线检测和硬件看门狗喂狗。

| 项目 | 值 |
| --- | --- |
| 健康检查周期 | `100 ms` |
| 看门狗超时 | `30000 ms` |
| 正常 LED 模式 | 500 ms 亮 / 500 ms 灭 |
| 故障 LED 模式 | 按故障优先级闪烁，随后暂停 3000 ms |
| 业务离线超时 | `3000 ms` |

已配置健康事件：

| 事件 | 默认启用 | 优先级 | 说明 |
| --- | --- | --- | --- |
| Ethernet | 否 | 1 | 网络健康预留 |
| Modbus TCP | 否 | 2 | TCP 服务健康预留 |
| 回转编码器读取 | 是 | 3 | RTU 读取成功后刷新 |
| 变幅编码器读取 | 是 | 4 | RTU 读取成功后刷新 |
| 起升编码器读取 | 是 | 5 | RTU 读取成功后刷新 |

当高优先级故障达到停止喂狗条件时，系统最终由硬件看门狗复位。当前表中默认启用的三路编码器事件优先级为 3、4、5，不会触发停止喂狗，但会改变 LED 故障显示并输出离线日志。

## 5. 调试诊断

### 5.1 串口诊断

连接 UART5，参数 `115200 8N1`。可观察启动日志、系统健康日志、Modbus 连接日志和 Shell。

常用 Shell 命令：

```text
fw_time
show_encoder_stats
clear_encoder_stats
kernel threads
net iface
net stats
log status
```

### 5.2 Telnet Shell

以太网连通后，可从同网段 PC 连接：

```powershell
telnet 192.168.18.32 23
```

如果 Windows 未启用 telnet 客户端，可使用 PuTTY、Tera Term 或其他 TCP 客户端连接 `192.168.18.32:23`。

### 5.3 网络诊断

PC 侧建议配置：

```text
IP:      192.168.18.4
Netmask: 255.255.255.0
Gateway: 192.168.18.1
```

基础连通性检查：

```powershell
ping 192.168.18.32
Test-NetConnection 192.168.18.32 -Port 502
Test-NetConnection 192.168.18.32 -Port 23
```

Shell 内可检查：

```text
net iface
net stats
net arp
```

### 5.4 Modbus TCP 诊断

推荐使用 Modbus Poll、QModMaster、modpoll 或测试脚本连接：

```text
IP: 192.168.18.32
Port: 502
Unit ID: 1
Protocol: Modbus TCP
```

重点读取：

```text
Input Registers: 0x0000 - 0x0011, quantity 18
Holding Registers: 0x0000, quantity 1
Coils: 0x0000, quantity 1
```

### 5.5 OTA 诊断

查看当前镜像状态：

```text
ota show
```

通过 mcumgr UDP 上传镜像时，目标端口为 `1337`。上传后可通过 Shell 执行：

```text
ota test
ota reboot
ota confirm
```

若测试镜像未在 `CONFIG_CRANER_OTA_TEST_TIMEOUT_S` 时间内确认，固件会自动重启并由 MCUboot 执行回滚。

## 6. 已知注意事项

1. 当前发布口径为 `v0.0.1`，但仓库 `VERSION` 文件当前为 `1.2.6`，出包前需要统一。
2. `main.c` 启动打印中提到 syslog UDP，但当前 `prj.conf` 启用的是 UART 日志后端；以当前配置为准。
3. 三路编码器离线事件默认启用，若测试现场没有接入编码器或模拟器，LED 会进入故障显示，`show_encoder_stats` 会出现失败计数。
4. MCUboot 当前使用开发签名配置，量产发布前需要替换为正式密钥和正式签名流程。
5. Modbus TCP 默认使用 502 端口，部分 PC 工具或防火墙策略可能需要管理员权限或放行规则。

## 7. 测试用例

### 7.1 测试环境

| 项目 | 要求 |
| --- | --- |
| DUT | Craner General STM32H743VIT6 PCB V1.1.0 |
| 调试器 | ST-LINK 或等效 SWD 调试器 |
| 串口工具 | 115200 8N1，连接 UART5 |
| 网络 | DUT 与 PC 位于 `192.168.18.0/24` |
| PC IP | 建议 `192.168.18.4` |
| Modbus TCP 工具 | Modbus Poll、QModMaster、modpoll 或自动化脚本 |
| Modbus RTU 编码器/模拟器 | 3 路，Unit ID `1`，9600 8E1 |
| OTA 工具 | `mcumgr` |

### 7.2 构建与烧录

| 用例 ID | 测试项 | 步骤 | 期望结果 |
| --- | --- | --- | --- |
| TC-BUILD-001 | 默认构建 | 执行 `.\build.ps1` | 构建成功，生成 MCUboot、应用和 OTA 镜像 |
| TC-BUILD-002 | 跳过 OTA 镜像构建 | 执行 `.\build.ps1 -SkipOtaImages` | 构建成功，生成可烧录固件 |
| TC-FLASH-001 | west 烧录 | 构建后执行 `.\flash.ps1` | 烧录成功，DUT 自动启动 |
| TC-FLASH-002 | 全量烧录 | 执行 `.\flash.ps1 -Target All` | bootloader 和 confirmed app 均烧录成功 |

### 7.3 启动与基础外设

| 用例 ID | 测试项 | 步骤 | 期望结果 |
| --- | --- | --- | --- |
| TC-BOOT-001 | 启动日志 | 烧录后打开 UART5 串口并复位 DUT | 串口输出应用启动信息，无致命错误 |
| TC-BOOT-002 | 控制台串口 | 串口输入回车 | 出现 Shell 提示符或可执行 Shell 命令 |
| TC-PWR-001 | 电源 GPIO 使能 | 上电后测量 3.3V、5V、网络桥接电源控制输出 | 对应电源被使能 |
| TC-LED-001 | 正常心跳 LED | 三路编码器均正常在线时观察 PD10 | LED 约 500 ms 亮 / 500 ms 灭 |
| TC-WDT-001 | 看门狗基础运行 | DUT 运行 10 分钟 | 系统不应无故复位 |

### 7.4 Shell 与日志

| 用例 ID | 测试项 | 步骤 | 期望结果 |
| --- | --- | --- | --- |
| TC-SHELL-001 | `fw_time` 命令 | 串口 Shell 执行 `fw_time` | 返回固件编译日期和时间 |
| TC-SHELL-002 | 编码器统计查看 | 执行 `show_encoder_stats` | 输出三路编码器 total、success、failure、success_rate 等字段 |
| TC-SHELL-003 | 编码器统计清零 | 执行 `clear_encoder_stats` 后再执行 `show_encoder_stats` | 统计计数被清零或重新从 0 开始累计 |
| TC-LOG-001 | UART 日志输出 | 触发 Modbus TCP 连接或编码器离线 | 串口可见对应日志，且日志限频生效 |
| TC-LOG-002 | 日志命令 | 执行 `log status` | Shell 返回日志系统状态 |

### 7.5 网络与 Telnet

| 用例 ID | 测试项 | 步骤 | 期望结果 |
| --- | --- | --- | --- |
| TC-NET-001 | 静态 IP 连通 | PC 执行 `ping 192.168.18.32` | ping 成功 |
| TC-NET-002 | Modbus TCP 端口 | PC 执行 `Test-NetConnection 192.168.18.32 -Port 502` | TCP 连接成功 |
| TC-NET-003 | Telnet 端口 | PC 执行 `Test-NetConnection 192.168.18.32 -Port 23` | TCP 连接成功 |
| TC-TELNET-001 | Telnet Shell 登录 | PC 连接 `192.168.18.32:23` | 进入 Zephyr Shell |
| TC-TELNET-002 | Telnet Shell 命令 | Telnet Shell 执行 `fw_time` | 返回编译日期和时间 |
| TC-NET-004 | 网络统计 | Shell 执行 `net stats` | 返回网络统计信息，无异常崩溃 |

### 7.6 Modbus TCP Server

| 用例 ID | 测试项 | 步骤 | 期望结果 |
| --- | --- | --- | --- |
| TC-MBTCP-001 | TCP 建连 | Modbus TCP 工具连接 `192.168.18.32:502`，Unit ID `1` | 连接成功 |
| TC-MBTCP-002 | 读取全部 Input Register | FC04 读取地址 `0x0000`，数量 `18` | 返回 18 个寄存器，无异常码 |
| TC-MBTCP-003 | 读取回转编码器数据 | FC04 读取 `0x0000` 到 `0x0005` | 返回时间戳、错误码、离线状态和 2 个业务值 |
| TC-MBTCP-004 | 读取变幅编码器数据 | FC04 读取 `0x0006` 到 `0x000B` | 返回时间戳、错误码、离线状态和 2 个业务值 |
| TC-MBTCP-005 | 读取起升编码器数据 | FC04 读取 `0x000C` 到 `0x0011` | 返回时间戳、错误码、离线状态和 2 个业务值 |
| TC-MBTCP-006 | Holding Register 读写 | FC06 写 `0x0000=0x1234`，再 FC03 读 `0x0000` | 读回 `0x1234` |
| TC-MBTCP-007 | Coil 读写 | FC05 写 `0x0000=ON`，再 FC01 读 `0x0000` | 读回 ON |
| TC-MBTCP-008 | 非法地址 | 读取未定义地址，例如 Input Register `0x0100` | 返回 Modbus 异常，不导致服务崩溃 |
| TC-MBTCP-009 | 断开重连 | 连续连接、读取、断开 20 次 | 每次均可重新连接并读取 |
| TC-MBTCP-010 | 自定义功能码 | 发送功能码 `101`，请求数据长度 2 字节 | 返回 `0x5555`、`0xAAAA` 和递增计数 |

### 7.7 Modbus RTU 编码器采集

| 用例 ID | 测试项 | 步骤 | 期望结果 |
| --- | --- | --- | --- |
| TC-RTU-001 | 回转编码器采集 | UART7 接入 Unit ID 1 模拟器，寄存器 `0x0002/0x0003` 输出固定值 | Modbus TCP 对应 `0x0004/0x0005` 读到固定值，离线状态为 0 |
| TC-RTU-002 | 变幅编码器采集 | UART8 接入 Unit ID 1 模拟器 | Modbus TCP 对应 `0x000A/0x000B` 读到固定值，离线状态为 0 |
| TC-RTU-003 | 起升编码器采集 | UART4 接入 Unit ID 1 模拟器 | Modbus TCP 对应 `0x0010/0x0011` 读到固定值，离线状态为 0 |
| TC-RTU-004 | 采集周期统计 | 三路模拟器在线运行 1 分钟，执行 `show_encoder_stats` | success 持续增加，failure 不应持续增加，平均成功间隔接近 50 ms |
| TC-RTU-005 | 单路断线检测 | 断开回转编码器 RS485 线路并等待 3 秒以上 | 回转 failure 增加，`REG_SLEWING_OFFLINE_STATUS=1`，LED 进入故障显示 |
| TC-RTU-006 | 单路恢复检测 | 重新接入回转编码器 | 回转 success 恢复增加，`REG_SLEWING_OFFLINE_STATUS=0` |
| TC-RTU-007 | 错误码记录 | 将模拟器 Unit ID 改为非 1 或关闭响应 | 对应 `ERROR_CODE` 变为非 0 |
| TC-RTU-008 | 三路同时在线 | 三路模拟器同时接入并运行 10 分钟 | 三路 success 均持续增加，系统不复位 |

### 7.8 系统健康与故障显示

| 用例 ID | 测试项 | 步骤 | 期望结果 |
| --- | --- | --- | --- |
| TC-HEALTH-001 | 编码器离线日志 | 断开任意一路编码器并等待 3 秒以上 | 串口日志输出 system health offline 事件 |
| TC-HEALTH-002 | 故障 LED 优先级 | 断开多路编码器 | LED 显示最高优先级离线事件对应闪烁模式 |
| TC-HEALTH-003 | 故障恢复 | 恢复所有编码器通信 | LED 回到正常心跳模式 |
| TC-HEALTH-004 | 长稳运行 | 三路编码器在线，DUT 运行 24 小时 | 无异常复位，无线程崩溃，Modbus TCP 可持续访问 |

### 7.9 OTA

| 用例 ID | 测试项 | 步骤 | 期望结果 |
| --- | --- | --- | --- |
| TC-OTA-001 | 查看 OTA 状态 | Shell 执行 `ota show` | 输出 active area、confirmed、next boot swap 和主备 bank 信息 |
| TC-OTA-002 | 上传升级包 | 使用 mcumgr UDP 上传 `app_update_signed.bin` 到 `192.168.18.32:1337` | 上传成功，slot1 存在新镜像 |
| TC-OTA-003 | test 升级并确认 | 执行 `ota test`、`ota reboot`，新镜像启动后执行 `ota confirm` | 新镜像被确认，后续重启不回滚 |
| TC-OTA-004 | test 升级回滚 | 执行 `ota test`、`ota reboot`，新镜像启动后不执行 `ota confirm`，等待超时 | 系统重启并回滚到旧镜像 |
| TC-OTA-005 | permanent 升级 | 执行 `ota permanent`、`ota reboot` | 新镜像永久生效 |
| TC-OTA-006 | 擦除 secondary | 执行 `ota erase-secondary` 后 `ota show` | secondary bank 被擦除或 header 不可用 |

### 7.10 异常与鲁棒性

| 用例 ID | 测试项 | 步骤 | 期望结果 |
| --- | --- | --- | --- |
| TC-ROBUST-001 | 网络断开恢复 | 运行中拔掉网线 30 秒再插回 | 系统不崩溃，网络恢复后 Modbus TCP/Telnet 可重新连接 |
| TC-ROBUST-002 | Modbus TCP 压力连接 | PC 以 5 到 10 Hz 读取 18 个 Input Register，持续 1 小时 | 无异常复位，读取成功率满足测试组阈值 |
| TC-ROBUST-003 | Telnet 与 Modbus 并发 | Telnet Shell 执行诊断命令，同时 Modbus TCP 持续读取 | 两者均可正常工作 |
| TC-ROBUST-004 | 编码器通信抖动 | 随机开关 RTU 模拟器响应 | failure/success 统计符合实际通信状态，系统不复位 |
| TC-ROBUST-005 | 非法 Modbus 请求 | 发送非法功能码、非法长度或越界地址 | 返回异常或关闭连接，系统不崩溃 |

## 8. 发布验收建议

v0.0.1 建议至少通过以下用例后再发布：

```text
TC-BUILD-001
TC-FLASH-001
TC-BOOT-001
TC-PWR-001
TC-SHELL-001
TC-NET-001
TC-NET-002
TC-TELNET-001
TC-MBTCP-002
TC-MBTCP-006
TC-RTU-001
TC-RTU-002
TC-RTU-003
TC-HEALTH-001
TC-OTA-001
TC-ROBUST-001
```

完整发布验收建议覆盖第 7 章全部测试用例，并保留串口日志、Modbus TCP 抓包、RTU 模拟器配置和 OTA 操作记录。
