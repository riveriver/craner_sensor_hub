# Zephyr 业务线程启用配置

## 这个示例实现了什么

本工程现在把主要业务线程做成了 Kconfig 可配置项。也就是说，不需要注释 C 代码，也不需要删除 `K_THREAD_DEFINE()`，只要在 `prj.conf` 或额外的配置文件里设置 `CONFIG_CRANER_ENABLE_*`，就可以决定固件里编译并启动哪些线程。

当前可配置的线程包括：

| Kconfig | 线程功能 | 默认值 |
| --- | --- | --- |
| `CONFIG_CRANER_ENABLE_HEARTBEAT_LED_THREAD` | PD10 心跳灯线程 | `y` |
| `CONFIG_CRANER_ENABLE_MODBUS_TCP_SERVER_THREAD` | Modbus TCP server 线程 | `y` |
| `CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD` | 回转编码器 RTU client 线程 | `y` |
| `CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD` | 变幅编码器 RTU client 线程 | `y` |
| `CONFIG_CRANER_ENABLE_READ_HOIST_ENCODER_THREAD` | 吊钩编码器 RTU client 线程 | `y` |
| `CONFIG_CRANER_ENABLE_LOG_SENSOR_THREAD` | sensor 日志示例线程 | `n` |
| `CONFIG_CRANER_ENABLE_LOG_COMM_THREAD` | comm 日志示例线程 | `n` |

## 怎么使用

默认配置在项目根目录 `prj.conf`：

```conf
CONFIG_CRANER_ENABLE_HEARTBEAT_LED_THREAD=y
CONFIG_CRANER_ENABLE_MODBUS_TCP_SERVER_THREAD=y
CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD=y
CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD=y
CONFIG_CRANER_ENABLE_READ_HOIST_ENCODER_THREAD=y
CONFIG_CRANER_ENABLE_LOG_SENSOR_THREAD=n
CONFIG_CRANER_ENABLE_LOG_COMM_THREAD=n
```

例如只想保留心跳灯和 Modbus TCP server，关闭三路 RTU 编码器线程：

```conf
CONFIG_CRANER_ENABLE_HEARTBEAT_LED_THREAD=y
CONFIG_CRANER_ENABLE_MODBUS_TCP_SERVER_THREAD=y
CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD=n
CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD=n
CONFIG_CRANER_ENABLE_READ_HOIST_ENCODER_THREAD=n
CONFIG_CRANER_ENABLE_LOG_SENSOR_THREAD=n
CONFIG_CRANER_ENABLE_LOG_COMM_THREAD=n
```

构建：

```powershell
.\build.ps1 -Board mp_rs485x4_stm32h743vit6
```

检查配置是否生效：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\craner_encoder_hub\zephyr\.config -Pattern "CONFIG_CRANER_ENABLE_"
```

## Kconfig：软件配置

应用级配置定义在项目根目录 `Kconfig`。文件开头必须有：

```kconfig
source "Kconfig.zephyr"
```

这行的作用是先加载 Zephyr 自己的 Kconfig 符号，例如 `CONFIG_GPIO`、`CONFIG_MODBUS`、`CONFIG_NET_TCP`。如果缺少这行，`prj.conf` 里的 Zephyr 标准配置会变成未定义符号，构建会失败。

线程开关示例：

```kconfig
config CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD
	bool "Enable slewing encoder Modbus RTU thread"
	default y
	depends on MODBUS
```

`depends on MODBUS` 表示只有启用了 Zephyr Modbus 子系统，这个线程开关才允许打开。

## CMake：按配置编译模块

`CMakeLists.txt` 根据 Kconfig 决定是否编译对应源文件：

```cmake
target_sources_ifdef(CONFIG_CRANER_ENABLE_HEARTBEAT_LED_THREAD app PRIVATE
	src/heartbeat_led_app.c
)

target_sources_ifdef(CONFIG_CRANER_ENABLE_MODBUS_TCP_SERVER_THREAD app PRIVATE
	src/modbus_tcp_server_app.c
)
```

RTU client 三路编码器共用同一个源文件，所以只要任意一路启用，就编译 `src/modbus_rtu_client_app.c`：

```cmake
if(CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD OR
   CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD OR
   CONFIG_CRANER_ENABLE_READ_HOIST_ENCODER_THREAD)
	target_sources(app PRIVATE src/modbus_rtu_client_app.c)
endif()
```

## 业务代码：线程如何被关闭

对于一个源文件对应一个线程的模块，例如心跳灯、Modbus TCP server，关闭 Kconfig 后源文件不会参与编译，线程自然不会存在。

对于三路 RTU 编码器，因为它们共用同一个源文件，代码内部还会按每一路开关分别保护：

```c
#if defined(CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD)
K_THREAD_DEFINE(slewing_encoder_tid, MODBUS_ENCODER_STACK_SIZE,
		modbus_encoder_thread, &slewing_encoder, NULL, NULL,
		MODBUS_ENCODER_PRIORITY, 0, 0);
#endif
```

这样关闭某一路时，对应的 DTS alias、client 结构体、shell 统计输出、线程定义都会一起消失。比如关闭变幅编码器线程后，即使板级 DTS 里没有 `modbus-luffing-encoder` alias，也不会因为这一路报 `BUILD_ASSERT`。

## 如何扩展

新增一个线程时，建议按这 3 步做：

1. 在 `Kconfig` 增加一个 `CONFIG_CRANER_ENABLE_xxx_THREAD`。
2. 在 `prj.conf` 给出默认值。
3. 在 `CMakeLists.txt` 用 `target_sources_ifdef()` 控制源文件是否编译。

如果多个线程共用一个源文件，就像 RTU 编码器一样，在源文件内部再用 `#if defined(CONFIG_xxx)` 保护每个线程实例。

## 故障排查

| 现象 | 检查点 |
| --- | --- |
| 改了 `prj.conf` 但没有生效 | 使用 pristine build，当前 `build.ps1` 已默认 `-p always` |
| `.config` 里没有 `CONFIG_CRANER_ENABLE_*` | 检查项目根目录是否有 `Kconfig`，且包含 `source "Kconfig.zephyr"` |
| 某个线程没有启动 | 检查 `.config` 里对应开关是否为 `y` |
| 关闭 RTU 某一路后仍然 DTS 报错 | 检查源文件里对应 `BUILD_ASSERT` 是否被同一个 Kconfig 包住 |
| 打开 Modbus TCP 失败 | 检查 `CONFIG_MODBUS`、`CONFIG_NET_TCP`、`CONFIG_NET_SOCKETS`、`CONFIG_POSIX_API` |
