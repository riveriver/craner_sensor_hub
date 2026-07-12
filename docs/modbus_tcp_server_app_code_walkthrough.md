# modbus_tcp_server_app.c 分段讲解

这篇文档专门解释 `src/modbus_tcp_server_app.c` 的代码。它不重复讲板卡怎么接线，而是帮助你读懂：为什么 Modbus TCP server 需要 socket、为什么又要 Modbus raw ADU、每个回调什么时候被调用。

## 1. 先看整体流程

这个文件做的事情可以理解为一条流水线：

```text
PC Modbus TCP client
        |
        | TCP 502 端口
        v
socket accept/recv
        |
        | 解析 Modbus TCP MBAP header
        v
modbus_raw_submit_rx()
        |
        | Zephyr Modbus core 根据功能码处理请求
        v
coil_rd / coil_wr / input_reg_rd / holding_reg_rd / holding_reg_wr
        |
        | server_raw_cb() 收到 Modbus core 生成的响应
        v
send() 发回 PC
```

最重要的一点：Zephyr 的 Modbus 子系统本身不直接监听 TCP 端口。当前代码用 socket 自己收 TCP 数据，然后把收到的 Modbus ADU 交给 Zephyr Modbus core。

## 2. 头文件分组

文件开头：

```c
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/modbus/modbus.h>
```

这些是基础能力：

| 头文件 | 用途 |
| --- | --- |
| `errno.h` | socket 出错时读取 `errno` |
| `string.h` | 使用 `memcpy()` |
| `zephyr/kernel.h` | 线程、信号量、sleep 等内核 API |
| `zephyr/sys/byteorder.h` | `sys_put_be16()` 写大端字节 |
| `zephyr/sys/util.h` | `BIT()`、`ARRAY_SIZE()`、`MIN()` |
| `zephyr/modbus/modbus.h` | Modbus API、ADU、回调结构 |

下面这组是 POSIX 风格 socket：

```c
#include <zephyr/posix/netinet/in.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/poll.h>
#include <zephyr/posix/netdb.h>

#include <zephyr/net/socket.h>
```

当前实现按 Zephyr 官方 sample 写法使用 `socket()`、`bind()`、`listen()`、`accept()`、`recv()`、`send()`、`close()`，所以 `prj.conf` 需要：

```conf
CONFIG_POSIX_API=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_TCP=y
```

## 3. 日志和宏

```c
LOG_MODULE_REGISTER(tcp_modbus, LOG_LEVEL_INF);

#define MODBUS_TCP_SERVER_STACK_SIZE 3072
#define MODBUS_TCP_SERVER_PRIORITY 8
#define MODBUS_TCP_PORT 502
```

含义：

| 宏 | 说明 |
| --- | --- |
| `tcp_modbus` | 日志模块名，串口日志会显示这个名字 |
| `MODBUS_TCP_SERVER_STACK_SIZE` | TCP server 线程栈大小 |
| `MODBUS_TCP_SERVER_PRIORITY` | TCP server 线程优先级 |
| `MODBUS_TCP_PORT` | Modbus TCP 标准端口 502 |

## 4. Modbus 数据表

```c
static int custom_read_count;
```

当前 server 不再直接保存 `holding_reg[]` 或 `coils_state`。真正的寄存器表放在 `src/modbus_register_map.c`，读写入口放在 `src/modbus_register_service.c`。

PC 访问 Modbus TCP 时：

| 功能码 | 数据来源 |
| --- | --- |
| FC01/FC05/FC15 | `coil_table[]` |
| FC03/FC06/FC16 | `holding_register_table[]` |
| FC04 | `input_register_table[]` |

这样 Modbus TCP server 和 Modbus RTU client 可以共享同一份寄存器状态。

`custom_read_count` 给自定义功能码 FC101 使用，每调用一次就加 1。

## 5. 自定义功能码 FC101

```c
static bool custom_handler(const int iface,
			   const struct modbus_adu *rx_adu,
			   struct modbus_adu *tx_adu,
			   uint8_t *const excep_code,
			   void *const user_data)
```

这个函数处理自定义功能码。参数可以这样理解：

| 参数 | 说明 |
| --- | --- |
| `rx_adu` | PC 发来的请求 |
| `tx_adu` | 准备发回 PC 的响应 |
| `excep_code` | 如果请求不合法，填 Modbus exception |
| `user_data` | 注册功能码时传进来的用户数据 |

它要求请求数据长度必须是 2：

```c
if (rx_adu->length != request_len) {
	*excep_code = MODBUS_EXC_ILLEGAL_DATA_VAL;
	return true;
}
```

如果长度不对，就返回 Modbus exception。

成功时，它往响应里写 3 个 16-bit 大端数据：

```c
sys_put_be16(0x5555, tx_adu->data);
sys_put_be16(0xAAAA, &tx_adu->data[2]);
sys_put_be16(*read_counter, &tx_adu->data[4]);
tx_adu->length = response_len;
```

响应内容是：

```text
0x5555
0xAAAA
调用次数 custom_read_count
```

注册自定义功能码：

```c
MODBUS_CUSTOM_FC_DEFINE(custom, custom_handler, 101, &custom_read_count);
```

这行会生成一个名为 `modbus_cfg_custom` 的配置对象，后面初始化时会注册给 Modbus core。

## 6. Coil 读写回调

读 coil：

```c
static int coil_rd(uint16_t addr, bool *state)
```

Zephyr Modbus core 收到 FC01 时，会按地址逐个调用它。

```c
return modbus_register_service_read_coil(addr, state);
```

写 coil：

```c
static int coil_wr(uint16_t addr, bool state)
```

Zephyr Modbus core 收到 FC05 或 FC15 时会调用它。

```c
return modbus_register_service_write_coil(
	addr, state);
```

地址是否存在、当前值如何更新，都由 `modbus_register_service` 统一处理。外部 client 只能通过标准功能码写 coil 和 holding register，input register 没有外部写入口。

## 7. Input / Holding Register 读写回调

读 Input Register：

```c
static int input_reg_rd(uint16_t addr, uint16_t *reg)
```

收到 FC04 时，Modbus core 会调用它。当前 input register 由 RTU 编码器线程更新，TCP client 只能读取。

```c
return modbus_register_service_read_input(addr, reg);
```

读 Holding Register：

```c
static int holding_reg_rd(uint16_t addr, uint16_t *reg)
```

收到 FC03 时，Modbus core 会调用它。

```c
return modbus_register_service_read_holding(addr, reg);
```

写 Holding Register：

```c
static int holding_reg_wr(uint16_t addr, uint16_t reg)
```

收到 FC06 或 FC16 时，Modbus core 会调用它。

```c
return modbus_register_service_write_holding(
	addr, reg);
```

所以你用 PC 工具 FC06 写地址 0 为 `0x1234`，写入会进入 `holding_register_table[]`，再用 FC03 读地址 0 就会读回这个值。

## 8. 把回调交给 Modbus core

```c
static struct modbus_user_callbacks mbs_cbs = {
	.coil_rd = coil_rd,
	.coil_wr = coil_wr,
	.input_reg_rd = input_reg_rd,
	.holding_reg_rd = holding_reg_rd,
	.holding_reg_wr = holding_reg_wr,
};
```

这张表告诉 Zephyr：

```text
收到 FC01 -> 调 coil_rd
收到 FC05/FC15 -> 调 coil_wr
收到 FC04 -> 调 input_reg_rd
收到 FC03 -> 调 holding_reg_rd
收到 FC06/FC16 -> 调 holding_reg_wr
```

当前代码没有注册 discrete input 回调，所以 FC02 不属于当前实现的重点。

## 9. tmp_adu、信号量和 server_iface

```c
static struct modbus_adu tmp_adu;
K_SEM_DEFINE(received, 0, 1);
static int server_iface;
```

这三个变量是 TCP 和 Modbus core 之间的桥。

| 变量 | 作用 |
| --- | --- |
| `tmp_adu` | 临时保存请求或响应的 Modbus ADU |
| `received` | 等待 Modbus core 处理完成的信号量 |
| `server_iface` | Zephyr Modbus raw interface 编号 |

为什么需要信号量？

因为 `modbus_raw_submit_rx()` 提交请求后，Modbus core 不是直接在当前函数里同步返回响应，而是通过 raw callback 把响应送回来。socket 线程需要等这个响应准备好，所以用 `k_sem_take()` 等待。

## 10. server_raw_cb：Modbus core 处理完后的出口

```c
static int server_raw_cb(const int iface, const struct modbus_adu *adu,
			 void *user_data)
```

这个函数不是 socket 调用的，而是 Zephyr Modbus core 调用的。

发生时机：

```text
modbus_raw_submit_rx()
        |
        v
Modbus core 解析功能码并调用业务回调
        |
        v
Modbus core 生成响应
        |
        v
server_raw_cb()
```

它做两件事。

第一，把 Modbus core 给出的响应复制到 `tmp_adu`：

```c
tmp_adu.trans_id = adu->trans_id;
tmp_adu.proto_id = adu->proto_id;
tmp_adu.length = adu->length;
tmp_adu.unit_id = adu->unit_id;
tmp_adu.fc = adu->fc;
memcpy(tmp_adu.data, adu->data,
       MIN(adu->length, CONFIG_MODBUS_BUFFER_SIZE));
```

第二，释放信号量，通知 socket 线程“响应好了”：

```c
k_sem_give(&received);
```

## 11. server_param：把 server 配成 raw 模式

```c
const static struct modbus_iface_param server_param = {
	.mode = MODBUS_MODE_RAW,
	.server = {
		.user_cb = &mbs_cbs,
		.unit_id = 1,
	},
	.rawcb.raw_tx_cb = server_raw_cb,
	.rawcb.user_data = NULL
};
```

关键点：

| 字段 | 说明 |
| --- | --- |
| `MODBUS_MODE_RAW` | 不让 Modbus 子系统自己操作串口，而是由应用提交 ADU |
| `user_cb = &mbs_cbs` | 普通功能码的读写回调表 |
| `unit_id = 1` | Modbus server 地址 |
| `raw_tx_cb = server_raw_cb` | Modbus core 生成响应后调用这个函数 |

Modbus TCP 需要 raw 模式，因为 TCP 收发由 socket 负责。

## 12. init_modbus_server：初始化 Modbus core

```c
static int init_modbus_server(void)
```

第一步：找到 raw interface：

```c
server_iface = modbus_iface_get_by_name("RAW_0");
```

`RAW_0` 来自配置：

```conf
CONFIG_MODBUS_RAW_ADU=y
CONFIG_MODBUS_NUMOF_RAW_ADU=1
```

第二步：初始化 server：

```c
modbus_init_server(server_iface, server_param);
```

第三步：注册自定义功能码：

```c
modbus_register_user_fc(server_iface, &modbus_cfg_custom);
```

如果只想做标准 Modbus 功能，可以删掉自定义 FC101 相关代码。

## 13. modbus_tcp_reply：把响应发回 PC

```c
static int modbus_tcp_reply(int client, struct modbus_adu *adu)
```

Modbus TCP 响应由两部分组成：

```text
MBAP header + PDU data
```

先生成 MBAP header：

```c
modbus_raw_put_header(adu, header);
```

再发 header：

```c
send(client, header, sizeof(header), 0)
```

再发 data：

```c
send(client, adu->data, adu->length, 0)
```

这里的 `client` 是 `accept()` 返回的 socket，代表当前连进来的 PC client。

## 14. modbus_tcp_connection：处理一次 Modbus 请求

```c
static int modbus_tcp_connection(int client)
```

这个函数处理一个完整请求和响应。

第一步：读取 MBAP header：

```c
rc = recv(client, header, sizeof(header), MSG_WAITALL);
```

`MSG_WAITALL` 表示尽量等到指定字节数都收齐。Modbus TCP header 长度固定，所以这里适合用它。

第二步：把 header 转成 `tmp_adu`：

```c
modbus_raw_get_header(&tmp_adu, header);
data_len = tmp_adu.length;
```

第三步：读取 PDU data：

```c
rc = recv(client, tmp_adu.data, data_len, MSG_WAITALL);
```

第四步：交给 Zephyr Modbus core：

```c
modbus_raw_submit_rx(server_iface, &tmp_adu)
```

这一步之后，Modbus core 会根据功能码调用前面的回调，比如：

```text
FC03 -> holding_reg_rd()
FC06 -> holding_reg_wr()
FC04 -> input_reg_rd()
FC01 -> coil_rd()
FC05 -> coil_wr()
FC101 -> custom_handler()
```

第五步：等待响应 ready：

```c
if (k_sem_take(&received, K_MSEC(1000)) != 0) {
	modbus_raw_set_server_failure(&tmp_adu);
}
```

如果 1 秒内没等到 `server_raw_cb()` 给信号量，就生成 server failure 异常响应。

第六步：发回 PC：

```c
return modbus_tcp_reply(client, &tmp_adu);
```

## 15. modbus_tcp_server_thread：创建 TCP server

```c
static void modbus_tcp_server_thread(void)
```

这个函数是线程主体。

第一步：初始化 Modbus：

```c
init_modbus_server()
```

第二步：创建 TCP socket：

```c
serv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
```

含义：

| 参数 | 说明 |
| --- | --- |
| `AF_INET` | IPv4 |
| `SOCK_STREAM` | TCP |
| `IPPROTO_TCP` | TCP 协议 |

第三步：绑定地址和端口：

```c
bind_addr.sin_family = AF_INET;
bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
bind_addr.sin_port = htons(MODBUS_TCP_PORT);
```

`INADDR_ANY` 表示监听本机所有 IPv4 地址。当前板子 IP 是 `192.168.18.32`，所以 PC 连接 `192.168.18.32:502` 就能进来。

第四步：绑定和监听：

```c
bind(serv, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
listen(serv, 5);
```

第五步：循环接受 client：

```c
client = accept(serv, (struct sockaddr *)&client_addr,
		&client_addr_len);
```

`accept()` 会阻塞等待 PC 连接。连接成功后返回新的 `client` socket。

第六步：在同一个连接里反复处理请求：

```c
do {
	rc = modbus_tcp_connection(client);
} while (!rc);
```

只要 `modbus_tcp_connection()` 返回 0，就继续处理下一条请求。返回错误说明连接断开或收发失败，于是关闭 client：

```c
close(client);
```

## 16. K_THREAD_DEFINE：让 server 自动运行

```c
K_THREAD_DEFINE(modbus_tcp_server_tid, MODBUS_TCP_SERVER_STACK_SIZE,
		modbus_tcp_server_thread, NULL, NULL, NULL,
		MODBUS_TCP_SERVER_PRIORITY, 0, 0);
```

这行代码的作用是：系统启动时自动创建一个线程，线程入口是 `modbus_tcp_server_thread()`。

参数解释：

| 参数 | 说明 |
| --- | --- |
| `modbus_tcp_server_tid` | 线程 ID 名字 |
| `MODBUS_TCP_SERVER_STACK_SIZE` | 栈大小 3072 |
| `modbus_tcp_server_thread` | 线程入口函数 |
| 三个 `NULL` | 传给线程入口的参数，这里不用 |
| `MODBUS_TCP_SERVER_PRIORITY` | 优先级 8 |
| 第一个 `0` | 线程选项 |
| 第二个 `0` | 启动延迟，0 表示立即启动 |

## 17. 一次 FC06 + FC03 的完整例子

假设 PC 做两步：

1. FC06 写 Holding Register 地址 0 为 `0x1234`。
2. FC03 读 Holding Register 地址 0。

写入时流程：

```text
PC send FC06
recv header
recv data
modbus_raw_submit_rx()
holding_reg_wr(addr=0, reg=0x1234)
modbus_register_service_write_holding()
server_raw_cb()
send response
```

读取时流程：

```text
PC send FC03
recv header
recv data
modbus_raw_submit_rx()
holding_reg_rd(addr=0, reg=&value)
modbus_register_service_read_holding()
server_raw_cb()
send response: 0x1234
```

所以这份代码的核心其实是两层：

```text
socket 层：负责 TCP 收发
Modbus core 层：负责解析功能码和调用寄存器回调
```

## 18. 最容易混淆的点

| 容易混淆的点 | 正确理解 |
| --- | --- |
| `tmp_adu` 是请求还是响应 | 两者都会用：先装请求，后来被 raw callback 覆盖成响应 |
| `server_raw_cb()` 谁调用 | Zephyr Modbus core 调用，不是 socket 调用 |
| `modbus_tcp_reply()` 为什么还要 header | Modbus TCP 响应必须有 MBAP header |
| `RAW_0` 从哪里来 | `CONFIG_MODBUS_RAW_ADU=y` 和 `CONFIG_MODBUS_NUMOF_RAW_ADU=1` |
| `INADDR_ANY` 是什么 IP | 不是一个具体 IP，表示监听所有本机 IP |
| 为什么需要 `K_SEM_DEFINE` | socket 线程要等 Modbus core 异步生成响应 |

## 19. 如果要简化代码

初学时可以暂时忽略或删除：

| 可以先忽略 | 原因 |
| --- | --- |
| FC101 custom function | 不是标准 Modbus 必需功能 |
| `LOG_HEXDUMP_DBG` | 只是调试原始字节 |
| coil 读写 | 如果只关心寄存器，先看 holding register |

最小理解路径是：

```text
modbus_tcp_server_thread()
  -> modbus_tcp_connection()
  -> modbus_raw_submit_rx()
  -> holding_reg_rd()/holding_reg_wr()
  -> modbus_register_service_read_holding()/write_holding()
  -> server_raw_cb()
  -> modbus_tcp_reply()
```

把这条路径看懂，就看懂了这个文件的大半。
