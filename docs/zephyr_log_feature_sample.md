# 使用 Zephyr 内置 log 命令控制日志

本文说明本项目如何使用 Zephyr logging 子系统，并通过 Shell 内置的 `log` 命令动态控制不同日志 tag 的输出级别。

当前实现保持最小化：

| 项目 | 配置 |
| --- | --- |
| 日志 tag | `sensor`、`comm` |
| 线程数量 | 2 个 |
| 控制方式 | Zephyr Shell 内置 `log` 命令 |
| 自定义日志控制命令 | 无 |
| `sensor` 编译级别 | debug |
| `comm` 编译级别 | debug |

## 一、功能目标

我们希望验证两个线程并发输出日志时，Zephyr logging 的行为是否符合预期，并且可以在运行时通过 Shell 控制每个 tag 的日志级别。

当前有两个独立线程：

```text
sensor thread -> 使用 sensor tag 输出日志
comm thread   -> 使用 comm tag 输出日志
```

每个线程代码里都会周期性调用 4 个级别的日志接口：

```text
error
warning
info
debug
```

最终能看到哪些日志，同时受两个条件限制：

1. 模块自己的编译级别。
2. Shell 中设置的运行时级别。

编译级别是上限。运行时不能打开已经在编译期裁掉的日志。

## 二、打开 Zephyr logging 配置

项目配置文件：

```text
prj.conf
```

相关配置如下：

```conf
CONFIG_LOG=y
CONFIG_LOG_RUNTIME_FILTERING=y
CONFIG_LOG_RUNTIME_DEFAULT_LEVEL=3
CONFIG_LOG_CMDS=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_LOG_MAX_LEVEL=4
```

配置说明：

| 配置 | 作用 |
| --- | --- |
| `CONFIG_LOG=y` | 启用 Zephyr logging 子系统 |
| `CONFIG_LOG_RUNTIME_FILTERING=y` | 允许运行时修改各日志源级别 |
| `CONFIG_LOG_RUNTIME_DEFAULT_LEVEL=3` | 启动后默认 runtime level 为 info |
| `CONFIG_LOG_CMDS=y` | 启用 Shell 内置 `log` 命令 |
| `CONFIG_LOG_DEFAULT_LEVEL=3` | 未单独指定的模块默认编译到 info |
| `CONFIG_LOG_MAX_LEVEL=4` | 系统允许最高编译到 debug |

注意：

```conf
CONFIG_LOG_DEFAULT_LEVEL=3
```

不要随便改成 `4`。如果全局默认是 debug，Zephyr 内部模块也可能输出大量 debug 日志，例如 `mpu`，导致串口刷屏和日志丢失。

本项目只让需要验证的模块按需设置编译级别，避免无关模块输出大量日志。

## 三、实现 sensor 日志线程

源码文件：

```text
src/log_app_sensor.c
```

关键代码：

```c
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define LOG_APP_SENSOR_STACK_SIZE 1024
#define LOG_APP_SENSOR_PRIORITY 5
#define LOG_APP_SENSOR_PERIOD K_MSEC(5000)

LOG_MODULE_REGISTER(sensor, LOG_LEVEL_DBG);

static void log_app_sensor_thread(void)
{
	while (1) {
		LOG_ERR("sensor error sample");
		LOG_WRN("sensor warning sample");
		LOG_INF("sensor info sample");
		LOG_DBG("sensor debug sample");
		k_sleep(LOG_APP_SENSOR_PERIOD);
	}
}

K_THREAD_DEFINE(log_app_sensor_tid, LOG_APP_SENSOR_STACK_SIZE, log_app_sensor_thread,
		NULL, NULL, NULL, LOG_APP_SENSOR_PRIORITY, 0, 0);
```

重点是：

```c
LOG_MODULE_REGISTER(sensor, LOG_LEVEL_DBG);
```

这行注册了一个日志源，名字是 `sensor`，并且该模块最高编译到 debug 级别。

因此 Shell 中可以通过 `sensor` 这个名字单独控制它：

```text
log enable dbg sensor
log disable sensor
```

因此可以通过 Shell 在运行时打开或关闭 `sensor` 的 debug 输出。

## 四、实现 comm 日志线程

源码文件：

```text
src/log_app_comm.c
```

关键代码：

```c
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define LOG_APP_COMM_STACK_SIZE 1024
#define LOG_APP_COMM_PRIORITY 5
#define LOG_APP_COMM_PERIOD K_MSEC(5000)

LOG_MODULE_REGISTER(comm, LOG_LEVEL_DBG);

static void log_app_comm_thread(void)
{
	while (1) {
		LOG_ERR("comm error sample");
		LOG_WRN("comm warning sample");
		LOG_INF("comm info sample");
		LOG_DBG("comm debug sample");
		k_sleep(LOG_APP_COMM_PERIOD);
	}
}

K_THREAD_DEFINE(log_app_comm_tid, LOG_APP_COMM_STACK_SIZE, log_app_comm_thread,
		NULL, NULL, NULL, LOG_APP_COMM_PRIORITY, 0, 0);
```

重点是：

```c
LOG_MODULE_REGISTER(comm, LOG_LEVEL_DBG);
```

这行注册了另一个日志源，名字是 `comm`，并且该模块最高编译到 debug 级别。

Shell 中可以单独控制：

```text
log enable dbg comm
log disable comm
```

因此可以通过 Shell 在运行时打开或关闭 `comm` 的 debug 输出。

## 五、加入编译

源码需要加入 `CMakeLists.txt`：

```cmake
target_sources(app PRIVATE
	src/main.c
	src/log_app_sensor.c
	src/log_app_comm.c
	src/shell_app.c
)
```

当前没有 `log_app.c` 聚合文件。每个日志线程放在自己的功能文件中：

```text
src/log_app_sensor.c
src/log_app_comm.c
```

## 六、编译和烧录

编译：

```powershell
.\build.ps1
```

烧录：

```powershell
.\flash.ps1
```

打开 UART5 串口终端，参数为：

| 参数 | 值 |
| --- | --- |
| 波特率 | 115200 |
| 数据位 | 8 |
| 校验位 | None |
| 停止位 | 1 |
| 流控 | None |

## 七、使用 Shell 内置 log 命令

进入 Shell 后，先查看帮助：

```text
log
```

或者：

```text
help
```

Zephyr 内置 `log` 命令常用形式如下：

```text
log enable <level> <module>
log disable <module>
```

注意：`disable` 后面不能带 level。下面这种写法是错误的：

```text
log disable dbg comm
```

Zephyr 会把 `dbg` 当成模块名，所以会提示：

```text
dbg: unknown source name.
```

如果想关闭 `comm`，使用：

```text
log disable comm
```

如果想把 `comm` 从 debug 降到 warning，使用：

```text
log enable wrn comm
```

也就是说：

| 目的 | 命令 |
| --- | --- |
| 设置某个模块的级别 | `log enable <level> <module>` |
| 关闭某个模块日志 | `log disable <module>` |

其中 `<level>` 常用值：

| level | 含义 |
| --- | --- |
| `err` | 只输出 error |
| `wrn` | 输出 error、warning |
| `inf` | 输出 error、warning、info |
| `dbg` | 输出 error、warning、info、debug |

### 只看 sensor 的 error 日志

```text
log enable dbg sensor
log disable comm
```

### 只看 comm 的 debug 日志

```text
log disable sensor
log enable dbg comm
```

### 两个 tag 都打开各自可用的最高级别

```text
log enable dbg sensor
log enable dbg comm
```

### 关闭某个 tag

```text
log disable sensor
```

### 关闭两个 tag

```text
log disable sensor
log disable comm
```

## 八、预期现象

默认配置：

```conf
CONFIG_LOG_RUNTIME_DEFAULT_LEVEL=3
```

启动后默认 runtime level 是 info，但每个模块还会受到自身编译级别限制：

| tag | 编译级别 | 默认可见日志 |
| --- | --- | --- |
| `sensor` | debug | error、warning、info |
| `comm` | debug | error、warning、info |

如果执行：

```text
log enable dbg sensor
```

则 `sensor debug sample` 会开始出现。

如果执行：

```text
log disable sensor
```

则 `sensor` tag 的日志不再输出，但 `comm` 不受影响。

如果执行：

```text
log enable dbg sensor
```

会让 `sensor debug sample` 出现，因为 `sensor` 当前已经编译到 debug。

## 九、常见问题

### 1. 串口刷出大量 mpu debug 日志

检查是否把全局默认级别设成了 debug：

```conf
CONFIG_LOG_DEFAULT_LEVEL=4
```

建议保持：

```conf
CONFIG_LOG_DEFAULT_LEVEL=3
```

需要更高等级日志的业务模块单独提高编译级别，例如：

```c
LOG_MODULE_REGISTER(sensor, LOG_LEVEL_DBG);
```

### 2. Shell 中没有 log 命令

检查是否启用：

```conf
CONFIG_LOG_CMDS=y
CONFIG_SHELL=y
```

### 3. 执行 log enable dbg sensor 后仍然没有 debug

先检查 `sensor` 模块是否编译到了 debug。当前代码应该是：

```c
LOG_MODULE_REGISTER(sensor, LOG_LEVEL_DBG);
```

如果被改成下面这样，debug 会在编译期被裁掉：

```c
LOG_MODULE_REGISTER(sensor, LOG_LEVEL_ERR);
```

同理，`comm` 当前也应该编译到 debug：

```c
LOG_MODULE_REGISTER(comm, LOG_LEVEL_DBG);
```

运行时不能打开已经被编译期裁掉的日志。

### 4. 出现 messages dropped

说明日志产生速度超过后端处理能力。可以降低日志频率，或减少 debug 输出。

当前两个线程周期都是 5000 ms，正常情况下不应持续刷出大量 dropped。

## 十、最小实现原则

本项目当前不额外封装日志控制命令。

不要再实现类似下面的自定义命令：

```text
log_app set sensor dbg
log_app list
```

因为 Zephyr 已经提供内置命令：

```text
log enable dbg sensor
log disable sensor
```

除非后续有明确业务需求，否则直接使用 Zephyr 原生命令即可。
