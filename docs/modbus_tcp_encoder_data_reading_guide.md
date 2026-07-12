# Modbus TCP 编码器数据读取接口说明

## 1. 文档用途

本文档面向上位机、调试工具和第三方系统，说明如何通过 Modbus TCP 读取 3 个编码器的数据。

3 个编码器：

| 机构 | 对外名称 |
| --- | --- |
| 回转 | `SLEWING` |
| 变幅 | `LUFFING` |
| 吊钩 | `HOISTING` |

## 2. 连接参数

| 参数 | 值 |
| --- | --- |
| 协议 | Modbus TCP |
| IP 地址 | `192.168.18.32` |
| TCP 端口 | `502` |
| Unit ID | `1` |
| 数据区 | Input Register |
| 读取功能码 | FC04 Read Input Registers |
| 数据格式 | 16 位无符号整数，高字节在前 |

## 4. 寄存器表

所有寄存器均为只读 Input Register。

| 地址 | 名称 | 机构 | 含义 |
| --- | --- | --- | --- |
| `0x0000` | `REG_SLEWING_TIMESTAMP_H` | 回转 | 上一次成功更新时间戳高 16 位 |
| `0x0001` | `REG_SLEWING_TIMESTAMP_L` | 回转 | 上一次成功更新时间戳低 16 位 |
| `0x0002` | `REG_SLEWING_ERROR_CODE` | 回转 | RTU通信错误码，`0` 表示正常 |
| `0x0003` | `REG_SLEWING_OFFLINE_STATUS` | 回转 | 离线状态，`0` 表示设备正常运行，`1` 表示设备发生了离线 |
| `0x0004` | `REG_SLEWING_TRUN_CNT` | 回转 | 圈数 |
| `0x0005` | `REG_SLEWING_SINAGLE_VAL` | 回转 | 单圈值 |
| `0x0006` | `REG_LUFFING_TIMESTAMP_H` | 变幅 | 上一次成功更新时间戳高 16 位 |
| `0x0007` | `REG_LUFFING_TIMESTAMP_L` | 变幅 | 上一次成功更新时间戳低 16 位 |
| `0x0008` | `REG_LUFFING_ERROR_CODE` | 变幅 | RTU通信错误码，`0` 表示正常 |
| `0x0009` | `REG_LUFFING_OFFLINE_STATUS` | 变幅 | 离线状态，`0` 表示设备正常运行，`1` 表示设备发生了离线 |
| `0x000A` | `REG_LUFFING_TRUN_CNT` | 变幅 | 圈数 |
| `0x000B` | `REG_LUFFING_SINAGLE_VAL` | 变幅 | 单圈值 |
| `0x000C` | `REG_HOISTING_TIMESTAMP_H` | 吊钩 | 上一次成功更新时间戳高 16 位 |
| `0x000D` | `REG_HOISTING_TIMESTAMP_L` | 吊钩 | 上一次成功更新时间戳低 16 位 |
| `0x000E` | `REG_HOISTING_ERROR_CODE` | 吊钩 | RTU通信错误码，`0` 表示正常 |
| `0x000F` | `REG_HOISTING_OFFLINE_STATUS` | 吊钩 | 离线状态，`0` 表示设备正常运行，`1` 表示设备发生了离线 |
| `0x0010` | `REG_HOISTING_TRUN_CNT` | 吊钩 | 圈数 |
| `0x0011` | `REG_HOISTING_SINAGLE_VAL` | 吊钩 | 单圈值 |


## 5. 两种读取方式

### 5.1 完整读取

完整读取适合需要时间戳、错误原因和完整诊断信息的场景。

一次 FC04 读取全部 18 个 Input Register：

| 参数 | 值 |
| --- | --- |
| Function Code | `04` |
| Start Address | `0x0000` |
| Quantity | `18` |

返回数据按 6 个寄存器一组解析：

| 机构 | 返回数组下标 | 协议地址范围 |
| --- | --- | --- |
| 回转 `SLEWING` | `[0]` 到 `[5]` | `0x0000` 到 `0x0005` |
| 变幅 `LUFFING` | `[6]` 到 `[11]` | `0x0006` 到 `0x000B` |
| 吊钩 `HOISTING` | `[12]` 到 `[17]` | `0x000C` 到 `0x0011` |

每组 6 个寄存器的含义：

| 组内偏移 | 字段 |
| --- | --- |
| `+0` | `TIMESTAMP_H` |
| `+1` | `TIMESTAMP_L` |
| `+2` | `ERROR_CODE` |
| `+3` | `OFFLINE_STATUS` |
| `+4` | `TRUN_CNT` |
| `+5` | `SINAGLE_VAL` |

### 5.2 最小读取

最小读取适合只关心“是否离线、圈数、单圈值”的场景。

每个编码器读取 3 个连续寄存器：

| 机构 | Function Code | Start Address | Quantity | 返回顺序 |
| --- | --- | --- | --- | --- |
| 回转 `SLEWING` | `04` | `0x0003` | `3` | 离线状态、圈数、单圈值 |
| 变幅 `LUFFING` | `04` | `0x0009` | `3` | 离线状态、圈数、单圈值 |
| 吊钩 `HOISTING` | `04` | `0x000F` | `3` | 离线状态、圈数、单圈值 |

最小读取不包含错误码和时间戳。如果 `OFFLINE_STATUS=1`，只能知道该编码器发生离线；如果需要进一步判断异常原因，请使用完整读取方式读取 `ERROR_CODE`。

## 6. 数据处理规则

### 6.1 时间戳

时间戳由两个 16 位寄存器组成一个 32 位无符号数：

```c
timestamp_ms = ((uint32_t)timestamp_h << 16) | timestamp_l;
```

note：当前版本的时间表示设备上电后的运行时间，时间戳单位为毫秒，

时间戳只在对应编码器数据成功更新时变化。通信异常时，时间戳保持上一次成功更新的时间。

### 6.2 离线状态

推荐判断逻辑：

```c
online = (offline_status == 0) && (error_code == 0);
```

如果使用最小读取方式，没有读取 `ERROR_CODE`，则使用：

```c
online = (offline_status == 0);
```

### 6.3 通信异常时的数据

通信异常时，设备不会清零圈数、单圈值和时间戳，而是保留上一次成功更新的数据。

因此：

| 字段 | 异常时含义 |
| --- | --- |
| `OFFLINE_STATUS=1` | 当前设备发生离线 |
| `ERROR_CODE!=0` | 当前通信错误原因 |
| `TRUN_CNT` | 上一次成功更新的圈数 |
| `SINAGLE_VAL` | 上一次成功更新的单圈值 |
| `TIMESTAMP_H/L` | 上一次成功更新时间 |

上位机不能仅凭圈数或单圈值是否非 0 判断设备状态，必须使用 `OFFLINE_STATUS`，完整读取时还应同时检查 `ERROR_CODE`。

### 6.4 位置计算

如果只需要原始数据，直接使用：

```text
圈数 = TRUN_CNT
单圈值 = SINAGLE_VAL
```

如果需要把圈数和单圈值换算成总计数，需要由上位机根据编码器单圈分辨率计算：
当前编码器的单圈分辨率single_turn_resolution = 8192
```c
position_count = turn_count * single_turn_resolution + single_value;
```

## 7. 通信错误码

`ERROR_CODE` 为 16 位无符号整数。

| 错误码 | 名称 | 含义 | 建议处理 |
| --- | --- | --- | --- |
| `0` | `OK` | 最近一次通信正常 | 使用当前数据 |
| `1` | `MODBUS_ILLEGAL_FUNCTION` | 数据源返回：不支持该功能码 | 检查设备协议/固件版本 |
| `2` | `MODBUS_ILLEGAL_DATA_ADDRESS` | 数据源返回：地址不合法 | 检查寄存器地址配置 |
| `3` | `MODBUS_ILLEGAL_DATA_VALUE` | 数据源返回：数据值不合法 | 检查读取数量或参数 |
| `4` | `MODBUS_SERVER_DEVICE_FAILURE` | 数据源返回：设备故障 | 检查设备状态 |
| `5` | `IO_ERROR_OR_MODBUS_ACK` | I/O 校验失败、响应不匹配，或数据源返回 ACK 异常 | 检查通信链路；若持续出现，记录现场数据 |
| `6` | `MODBUS_SERVER_DEVICE_BUSY` | 数据源返回：设备忙 | 稍后重试 |
| `8` | `MODBUS_MEMORY_PARITY_ERROR` | 数据源返回：存储/奇偶校验错误 | 检查设备状态 |
| `10` | `MODBUS_GATEWAY_PATH_UNAVAILABLE` | 数据源返回：网关路径不可用 | 检查中间设备或数据源 |
| `11` | `MODBUS_GATEWAY_TARGET_FAILED_TO_RESPOND` | 数据源返回：目标无响应 | 检查数据源连接和供电 |
| `19` | `ENODEV` | 设备或通信接口不可用 | 检查设备是否正常启动 |
| `22` | `EINVAL` | 参数或响应格式不合法 | 检查读取数量、地址和固件版本 |
| `116` | `ETIMEDOUT` | 通信超时 | 检查设备连接、供电和现场干扰 |
| `122` | `EMSGSIZE` | 通信帧长度异常 | 检查通信干扰或协议配置 |
| `134` | `ENOTSUP` | 当前功能或模式不支持 | 检查固件版本和配置 |
| 其他非 0 | `UNKNOWN_ERROR` | 未归类错误 | 按通信异常处理，并记录错误码 |

说明：

1. `ERROR_CODE=0` 时，`OFFLINE_STATUS` 应为 `0`。
2. `ERROR_CODE!=0` 时，`OFFLINE_STATUS` 应为 `1`。
3. 错误码 `5` 在当前接口中可能表示 I/O 校验类错误，也可能是标准 Modbus exception `ACK`，对上位机而言都应按“通信异常，需要重试或提示”处理。

## 8. 完整读取解析示例

假设 FC04 读取 `0x0000`、数量 `18`，返回：

```text
0000 7D00 0000 0000 02FC 00AD
0000 7D02 0000 0000 0A76 03F3
0000 7D04 0074 0001 1693 0000
```

解析结果：

| 机构 | 时间戳 | 错误码 | 离线状态 | 圈数 | 单圈值 | 状态 |
| --- | --- | --- | --- | --- | --- | --- |
| 回转 | `0x00007D00 = 32000 ms` | `0` | `0` | `0x02FC` | `0x00AD` | 正常 |
| 变幅 | `0x00007D02 = 32002 ms` | `0` | `0` | `0x0A76` | `0x03F3` | 正常 |
| 吊钩 | `0x00007D04 = 32004 ms` | `116` | `1` | `0x1693` | `0x0000` | 通信超时，数据为上一次成功值 |

## 9. 最小读取解析示例

读取回转：

```text
Function Code: 04
Start Address: 0x0003
Quantity: 3
```

假设返回：

```text
0000 02FC 00AD
```

解析：

| 字段 | 值 |
| --- | --- |
| `OFFLINE_STATUS` | `0`，正常 |
| `TRUN_CNT` | `0x02FC` |
| `SINAGLE_VAL` | `0x00AD` |

如果返回：

```text
0001 02FC 00AD
```

表示当前通信异常，但 `0x02FC` 和 `0x00AD` 仍是上一次成功更新的数据。

## 10. 推荐读取流程

完整读取流程：

1. 连接 `192.168.18.32:502`。
2. 使用 FC04 读取 `0x0000`、数量 `18`。
3. 每 6 个寄存器解析一个编码器。
4. 使用 `OFFLINE_STATUS` 和 `ERROR_CODE` 判断状态。
5. 如果状态正常，使用圈数和单圈值。
6. 如果状态异常，显示异常状态。

最小读取流程：

1. 分别读取 `0x0003/0x0009/0x000F`，每次数量 `3`。
2. 每组返回顺序都是：离线状态、圈数、单圈值。
3. 使用 `OFFLINE_STATUS` 判断状态。
4. 如果 `OFFLINE_STATUS=1`，显示离线；如需错误原因，再执行完整读取。

## 11. 常见问题

| 现象 | 处理 |
| --- | --- |
| 连接不上 `192.168.18.32:502` | 先确认网络可达，再确认端口 `502` 未被防火墙拦截 |
| FC04 读取失败 | 确认读取的是 Input Register，并检查地址是否使用协议地址 |
| 工具里填 `0` 读不到 | 尝试填 `30001`，不同工具地址显示规则不同 |
| 圈数/单圈值有值但显示异常 | 正常，异常时会保留上一次成功数据，应以 `OFFLINE_STATUS` 判断状态 |
| 时间戳不变化 | 表示该编码器没有新的成功更新，检查 `OFFLINE_STATUS` 和 `ERROR_CODE` |
| `ERROR_CODE=116` | 通信超时，检查现场连接、供电和干扰 |
