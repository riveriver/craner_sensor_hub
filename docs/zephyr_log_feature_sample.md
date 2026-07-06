# Zephyr Logging 示例：模块化日志

## 1. 示例实现了什么

本示例演示 Zephyr logging 子系统的基本用法：为不同业务模块注册不同日志模块名，并使用 `LOG_ERR()`、`LOG_WRN()`、`LOG_INF()`、`LOG_DBG()` 输出不同等级的日志。

当前示例文件：

| 文件 | 日志模块名 | 状态 |
| --- | --- | --- |
| `src/log_app_sensor.c` | `sensor` | 已编译，线程启动语句当前注释 |
| `src/log_app_comm.c` | `comm` | 已编译，线程启动语句当前注释 |

注意：当前两个示例线程的 `K_THREAD_DEFINE()` 是注释状态，所以默认固件不会周期性打印 sensor/comm 日志。文档仍说明如何从零实现和启用它。

## 2. 怎么使用

当前默认固件已经启用 logging 子系统，Shell 中可以使用日志命令：

```text
log status
log list
log enable sensor 4
log enable comm 4
```

如果要让示例线程真正运行，需要取消下面代码的注释。

`src/log_app_sensor.c`：

```c
K_THREAD_DEFINE(log_app_sensor_tid, LOG_APP_SENSOR_STACK_SIZE, log_app_sensor_thread,
		NULL, NULL, NULL, LOG_APP_SENSOR_PRIORITY, 0, 0);
```

`src/log_app_comm.c`：

```c
K_THREAD_DEFINE(log_app_comm_tid, LOG_APP_COMM_STACK_SIZE, log_app_comm_thread,
		NULL, NULL, NULL, LOG_APP_COMM_PRIORITY, 0, 0);
```

启用后编译烧录，期望每 5 秒看到类似日志：

```text
<err> sensor: sensor error sample
<wrn> sensor: sensor warning sample
<inf> sensor: sensor info sample
<dbg> sensor: sensor debug sample
```

## 3. 前置条件

需要：

| 项目 | 说明 |
| --- | --- |
| Console/Shell | 用于查看日志和输入 `log` 命令 |
| Kconfig | 启用 `CONFIG_LOG` 和 `CONFIG_LOG_CMDS` |
| CMake | 示例源文件加入 `CMakeLists.txt` |
| 线程 | 如果要自动打印，需要启用 `K_THREAD_DEFINE()` |

## 4. 设备树：硬件描述

Logging 本身不直接依赖特殊硬件，但日志输出需要 console。console 的硬件描述仍来自 DTS。

Craner 板：

```dts
chosen {
	zephyr,console = &uart5;
};
```

MP 板：

```dts
chosen {
	zephyr,console = &usart1;
};
```

如果 console UART 没有启用，日志即使生成了也看不到串口输出。

## 5. Kconfig/prj.conf：软件配置

当前配置：

```conf
CONFIG_LOG=y
CONFIG_LOG_RUNTIME_FILTERING=y
CONFIG_LOG_RUNTIME_DEFAULT_LEVEL=3
CONFIG_LOG_CMDS=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_LOG_MAX_LEVEL=4
```

含义：

| 配置 | 作用 |
| --- | --- |
| `CONFIG_LOG=y` | 启用日志子系统 |
| `CONFIG_LOG_RUNTIME_FILTERING=y` | 允许运行时调整日志等级 |
| `CONFIG_LOG_RUNTIME_DEFAULT_LEVEL=3` | 运行时默认等级为 info |
| `CONFIG_LOG_CMDS=y` | 启用 Shell 中的 `log` 命令 |
| `CONFIG_LOG_DEFAULT_LEVEL=3` | 编译期默认日志等级为 info |
| `CONFIG_LOG_MAX_LEVEL=4` | 允许编译 debug 日志 |

Zephyr 常用等级数字：

| 数字 | 等级 |
| --- | --- |
| 1 | error |
| 2 | warning |
| 3 | info |
| 4 | debug |

## 6. 业务/应用代码

日志模块先注册：

```c
LOG_MODULE_REGISTER(sensor, LOG_LEVEL_DBG);
```

然后在业务逻辑中输出：

```c
LOG_ERR("sensor error sample");
LOG_WRN("sensor warning sample");
LOG_INF("sensor info sample");
LOG_DBG("sensor debug sample");
```

线程逻辑：

```c
static void log_app_sensor_thread(void)
{
	while (1) {
		LOG_INF("sensor info sample");
		k_sleep(K_MSEC(5000));
	}
}
```

把源文件加入 `CMakeLists.txt`：

```cmake
target_sources(app PRIVATE
	src/log_app_sensor.c
	src/log_app_comm.c
)
```

## 7. 如何扩展

新增业务模块时，建议每个模块单独注册日志名：

```c
LOG_MODULE_REGISTER(encoder, LOG_LEVEL_INF);
```

这样运行时可以单独调整：

```text
log enable encoder 4
log disable encoder
```

如果日志太多，可以降低默认等级：

```conf
CONFIG_LOG_RUNTIME_DEFAULT_LEVEL=2
```

## 8. 常见问题排查

| 现象 | 检查项 |
| --- | --- |
| 没有 sensor/comm 周期日志 | 当前 `K_THREAD_DEFINE()` 是注释状态 |
| debug 日志不显示 | 检查 `CONFIG_LOG_MAX_LEVEL=4`，并用 `log enable <module> 4` |
| `log` 命令不存在 | 检查 `CONFIG_LOG_CMDS=y` 和 `CONFIG_SHELL=y` |
| 编译 warning `defined but not used` | 说明线程函数存在但启动宏被注释 |

生成配置检查：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\.config -Pattern "CONFIG_LOG|CONFIG_LOG_CMDS"
```
