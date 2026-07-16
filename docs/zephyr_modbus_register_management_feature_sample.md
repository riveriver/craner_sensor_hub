# Zephyr Modbus Register 管理示例

## 1. 示例实现了什么

本示例把项目里的 Modbus 寄存器从具体业务模块中抽出来，形成两层结构：

| 模块 | 职责 |
| --- | --- |
| `modbus_register_map` | 维护本项目的寄存器表、默认值和当前值 |
| `modbus_register_service` | 注册寄存器表，提供统一读写 API，做范围检查和互斥保护 |

当前暂时不做持久化存储，也不划分 flash。所有寄存器值都保存在 RAM 中，上电后恢复 `default_value`，运行过程中由 Modbus TCP server 或内部 RTU 采集线程更新。

当前寄存器规则：

| 类型 | 读写规则 | 上电行为 |
| --- | --- | --- |
| Coil | 外部 client 可读可写，内部代码也可写 | 恢复默认值 |
| Input Register | 外部 client 只读，内部代码可写 | 恢复默认值，等待 RTU 线程刷新 |
| Holding Register | 外部 client 可读可写，内部代码也可写 | 恢复默认值 |

当前 Input Register 分配：

| 地址 | 名称 | 含义 |
| --- | --- | --- |
| `0x0000` | `REG_SLEWING_TIMESTAMP_H` | 回转上一次成功更新时间戳高 16 位 |
| `0x0001` | `REG_SLEWING_TIMESTAMP_L` | 回转上一次成功更新时间戳低 16 位 |
| `0x0002` | `REG_SLEWING_ERROR_CODE` | 回转最近一次通信错误码，成功为 `0` |
| `0x0003` | `REG_SLEWING_OFFLINE_STATUS` | 回转离线状态，成功为 `0`，失败为 `1` |
| `0x0004` | `REG_SLEWING_TRUN_CNT` | 回转圈数 |
| `0x0005` | `REG_SLEWING_SINAGLE_VAL` | 回转单圈值 |
| `0x0006` | `REG_LUFFING_TIMESTAMP_H` | 变幅上一次成功更新时间戳高 16 位 |
| `0x0007` | `REG_LUFFING_TIMESTAMP_L` | 变幅上一次成功更新时间戳低 16 位 |
| `0x0008` | `REG_LUFFING_ERROR_CODE` | 变幅最近一次通信错误码，成功为 `0` |
| `0x0009` | `REG_LUFFING_OFFLINE_STATUS` | 变幅离线状态，成功为 `0`，失败为 `1` |
| `0x000A` | `REG_LUFFING_TRUN_CNT` | 变幅圈数 |
| `0x000B` | `REG_LUFFING_SINAGLE_VAL` | 变幅单圈值 |
| `0x000C` | `REG_HOISTING_TIMESTAMP_H` | 吊钩上一次成功更新时间戳高 16 位 |
| `0x000D` | `REG_HOISTING_TIMESTAMP_L` | 吊钩上一次成功更新时间戳低 16 位 |
| `0x000E` | `REG_HOISTING_ERROR_CODE` | 吊钩最近一次通信错误码，成功为 `0` |
| `0x000F` | `REG_HOISTING_OFFLINE_STATUS` | 吊钩离线状态，成功为 `0`，失败为 `1` |
| `0x0010` | `REG_HOISTING_TRUN_CNT` | 吊钩圈数 |
| `0x0011` | `REG_HOISTING_SINAGLE_VAL` | 吊钩单圈值 |

其他寄存器：

| 类型 | 地址 | 名称 | 含义 |
| --- | --- | --- | --- |
| Coil | `0x0000` | `REG_COIL_RESERVER` | 系统复位控制位 |
| Holding Register | `0x0000` | `REG_HOLDING_RESERVER` | 测试用 |

## 2. 怎么使用

编译：

```powershell
.\build.ps1
.\build.ps1 -Board craner_general_stm32h743vit6
```

启动后串口日志应能看到：

```text
Registered Modbus register map: coils=1 inputs=18 holdings=1
Project Modbus register map is ready
Started MODBUS TCP server example on port 502
```

PC 侧使用 Modbus TCP client 连接：

| 参数 | 值 |
| --- | --- |
| IP | `192.168.18.32` |
| Port | `502` |
| Unit ID | `1` |

可测试：

| 功能码 | 操作 |
| --- | --- |
| FC01 | 读 Coil `0x0000` |
| FC05/FC15 | 写 Coil `0x0000` |
| FC03 | 读 Holding Register `0x0000` |
| FC06/FC16 | 写 Holding Register `0x0000` |
| FC04 | 读 Input Register `0x0000` 到 `0x0011` |

例如 UART7 回转编码器远端 `0x0002` 读到 `0x1234`，远端 `0x0003` 读到 `0x5678`，RTU 线程会写入：

```c
REG_SLEWING_TRUN_CNT = 0x1234
REG_SLEWING_SINAGLE_VAL = 0x5678
REG_SLEWING_ERROR_CODE = 0
REG_SLEWING_OFFLINE_STATUS = 0
REG_SLEWING_TIMESTAMP_H/L = k_uptime_get_32()
```

如果通信失败，只写入错误码并把 `OFFLINE_STATUS` 置为 `1`。时间戳、圈数和单圈值保持上一次成功读取的值。

## 3. 前置条件

| 项目 | 说明 |
| --- | --- |
| Ethernet | Modbus TCP server 正常启动 |
| RTU 串口 | UART7/UART8/UART4 的 DTS alias 已存在 |
| Modbus 配置 | `CONFIG_MODBUS=y` 和 client/server 角色已启用 |
| 线程安全 | 多线程访问寄存器由 `modbus_register_service` 内部互斥锁保护 |
| 持久化 | 当前未启用，重启后恢复默认值 |

## 4. Devicetree：硬件描述

寄存器管理本身不直接依赖硬件 DTS。它依赖业务模块已经配置好的硬件入口。

RTU 采集依赖 3 个 Modbus serial alias：

```dts
aliases {
	modbus-slewing-encoder = &modbus_slewing_encoder;
	modbus-luffing-encoder = &modbus_luffing_encoder;
	modbus-hook-encoder = &modbus_hook_encoder;
};
```

这些 alias 让 `modbus_rtu_client_app.c` 找到 3 个 RTU 接口，并把读取到的数据写入 input register。以太网 DTS 由 Modbus TCP server 使用，只要网络接口正常，TCP client 就能通过 Modbus 地址访问 `modbus_register_service`。

当前不在板级 DTS 中划分 `storage_partition`，也不设置 `zephyr,code-partition`。flash 分区后续会统一规划。

## 5. Kconfig/prj.conf：软件配置

寄存器管理模块本身只依赖 Zephyr kernel 和日志：

```conf
CONFIG_LOG=y
```

因为它被 Modbus TCP 和 RTU 使用，所以项目还需要：

```conf
CONFIG_MODBUS=y
CONFIG_MODBUS_ROLE_CLIENT_SERVER=y
CONFIG_MODBUS_RAW_ADU=y
CONFIG_MODBUS_NUMOF_RAW_ADU=1
CONFIG_NET_TCP=y
CONFIG_NET_SOCKETS=y
CONFIG_POSIX_API=y
```

当前不启用这些存储相关配置：

```conf
# CONFIG_FLASH is not required by modbus_register
# CONFIG_FLASH_MAP is not required by modbus_register
# CONFIG_USE_DT_CODE_PARTITION is not used
# CONFIG_NVS is not used
```

## 6. 业务/应用代码

### 6.1 `modbus_register_map`

`src/modbus_register_map.c` 维护本项目的寄存器表：

```c
static struct modbus_register_coil coil_table[] = { ... };
static struct modbus_register_input input_register_table[] = { ... };
static struct modbus_register_holding holding_register_table[] = { ... };
```

每个表项包含：

| 字段 | 作用 |
| --- | --- |
| `name` | 给应用层按名字访问，也用于日志 |
| `addr` | 对外 Modbus 地址 |
| `default_value` | 上电默认值 |
| `value` | 当前值 |

初始化时只注册 map 并初始化默认值：

```c
modbus_register_service_register_map(&app_register_map);
modbus_register_service_init();
```

### 6.2 `modbus_register_service`

`src/modbus_register_service.c` 统一提供按地址和按名字两类访问 API：

```c
modbus_register_service_read_input(0x0004, &value);
modbus_register_service_read_input_by_name("REG_SLEWING_TRUN_CNT", &value);

modbus_register_service_write_input(0x0004, value);
modbus_register_service_write_input_by_name("REG_SLEWING_TRUN_CNT", value);

const uint16_t values[] = {
	0,
	1,
	turn_count,
	single_value,
};

modbus_register_service_write_inputs_by_name(
	"REG_SLEWING_ERROR_CODE", values, ARRAY_SIZE(values));
```

推荐分工：

| 使用者 | 推荐访问方式 | 原因 |
| --- | --- | --- |
| Modbus TCP server 回调 | 地址 | TCP client 天然按 Modbus 地址访问 |
| 应用业务代码 | 名字 | 改寄存器地址时不需要跟着改业务逻辑 |
| 应用批量更新 | 起始名字 + 数量 | 一次通信结果整理后一次写入，只进入一次互斥锁 |
| 范围读写 | 起始地址 + 数量 | Modbus 连续寄存器访问本质是地址范围 |

外部 Modbus TCP client 只能通过标准功能码写 coil 和 holding register。input register 没有标准写功能码，所以不会暴露外部写入口；input 写 API 只给内部业务代码调用。

### 6.3 Modbus TCP 接入

`src/modbus_tcp_server_app.c` 的 Zephyr 回调只负责转发：

```c
coil_rd()        -> modbus_register_service_read_coil()
coil_wr()        -> modbus_register_service_write_coil()
input_reg_rd()   -> modbus_register_service_read_input()
holding_reg_rd() -> modbus_register_service_read_holding()
holding_reg_wr() -> modbus_register_service_write_holding()
```

TCP server 不维护自己的 `holding_reg[]` 或 `coils_state`，所有值都来自共享寄存器表。

### 6.4 Modbus RTU 接入

`src/modbus_rtu_client_app.c` 每次通信都会把结果写入 input register，而且应用层使用名字访问：

```c
const uint16_t values[] = {
	timestamp_h,
	timestamp_l,
	error_code,
	offline_status,
	turn_count,
	single_value,
};

modbus_register_service_write_inputs_by_name(
	encoder->timestamp_high_name, values, ARRAY_SIZE(values));
```

这表示 input register 是内部数据出口：RTU 采集线程负责更新离线状态、错误码、时间戳、圈数和单圈值；TCP client 负责通过 FC04 读取。

## 7. 如何扩展

| 需求 | 修改位置 |
| --- | --- |
| 增加 coil | `src/modbus_register_map.c` 的 `coil_table[]` |
| 增加 input register | `input_register_table[]` |
| 增加 holding register | `holding_register_table[]` |
| 修改默认值 | 表项的 `default_value` |
| 修改 RTU 数据映射 | `modbus_rtu_client_app.c` 的 `*_name` 字段 |
| 后续增加持久化 | 新增独立存储模块，并统一规划 flash 分区后再接入 service |
| 新项目复用 | 保留 `service`，替换自己的 `map` |

## 8. 常见问题排查

| 现象 | 检查 |
| --- | --- |
| FC04 读 input 一直是 0 | RTU 线程是否启动，编码器是否通信成功，`OFFLINE_STATUS` 和 `ERROR_CODE` 是多少 |
| 写 input register 失败 | 外部 client 不能写 input，只能内部业务写 |
| 按名字写入返回 `-ENOTSUP` | `modbus_register_map.c` 中是否存在完全一致的 `name` 字符串 |
| 写 coil/holding 后重启没有恢复 | 当前未实现持久化，这是预期行为 |
| 编译找不到 `modbus_register_service_write_inputs_by_name` | 确认头文件和 `src/modbus_register_service.c` 都已更新并加入 `CMakeLists.txt` |

生成配置检查：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\.config -Pattern "CONFIG_MODBUS|CONFIG_NET_TCP|CONFIG_NET_SOCKETS|CONFIG_NVS|CONFIG_USE_DT_CODE_PARTITION"
```

当前期望看不到 `CONFIG_NVS=y` 和 `CONFIG_USE_DT_CODE_PARTITION=y`。
