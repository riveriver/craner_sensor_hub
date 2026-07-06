# Zephyr Shell 交互功能实现说明

本文面向刚接触本项目的同事，介绍如何在 Zephyr 中启用 Shell 交互、体验系统内置 Shell 指令，以及如何添加自定义 Shell 指令。

本项目当前 Shell 使用 UART5：

| 项目 | 配置 |
| --- | --- |
| 板卡 | `craner_general_stm32h743vit6` |
| 串口 | UART5 |
| TX | PB6 |
| RX | PB5 |
| 波特率 | 115200 |
| Shell 提示符 | `craner:~$ ` |

## Shell 是什么

Zephyr Shell 是一个命令行交互模块。启用后，可以通过串口终端输入命令，查看系统状态、执行调试命令，或者调用我们自己注册的业务命令。

简单理解：

```text
PC 串口终端
  -> USB 转串口模块
  -> 板子 UART5 RX
  -> Zephyr Shell
  -> 执行内置命令或自定义命令
  -> 通过 UART5 TX 返回结果
```

Shell 常用于：

| 场景 | 说明 |
| --- | --- |
| 调试系统状态 | 查看 kernel、device、uptime 等信息 |
| 快速验证功能 | 不重新烧录固件，通过命令触发某个动作 |
| 产测辅助 | 查询版本、编译时间、硬件状态 |
| 现场诊断 | 通过串口输入命令获取设备内部信息 |

## 一、配置 Shell

### 1. 配置 Shell 使用的串口

板级 DTS 文件：

```text
boards/craner/craner_general_stm32h743vit6/craner_general_stm32h743vit6.dts
```

在 `chosen` 节点中指定 Shell 使用的 UART：

```dts
/ {
	chosen {
		zephyr,sram = &sram0;
		zephyr,flash = &flash0;
		zephyr,console = &uart5;
		zephyr,shell-uart = &uart5;
	};
};
```

关键配置是：

```dts
zephyr,shell-uart = &uart5;
```

它告诉 Zephyr：Shell 后端使用 `uart5`。

本项目同时配置了：

```dts
zephyr,console = &uart5;
```

所以 `printf()`、`printk()` 和 Shell 都走 UART5。

### 2. 启用 UART5

同一个 DTS 文件中需要启用 UART5：

```dts
&uart5 {
	pinctrl-0 = <&uart5_tx_pb6 &uart5_rx_pb5>;
	pinctrl-names = "default";
	current-speed = <115200>;
	status = "okay";
};
```

这表示：

| 配置 | 含义 |
| --- | --- |
| `uart5_tx_pb6` | UART5 TX 使用 PB6 |
| `uart5_rx_pb5` | UART5 RX 使用 PB5 |
| `current-speed = <115200>` | 串口波特率为 115200 |
| `status = "okay"` | 启用 UART5 |

USB 转串口接线：

```text
板子 PB6 / UART5_TX  ->  USB 转串口 RX
板子 PB5 / UART5_RX  ->  USB 转串口 TX
板子 GND             ->  USB 转串口 GND
```

### 3. 修改 prj.conf

项目配置文件：

```text
prj.conf
```

启用 Shell 需要以下配置：

```conf
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_SHELL_PROMPT_UART="craner:~$ "
```

配置说明：

| 配置 | 作用 |
| --- | --- |
| `CONFIG_SERIAL=y` | 启用串口驱动 |
| `CONFIG_CONSOLE=y` | 启用 Zephyr console |
| `CONFIG_UART_CONSOLE=y` | 使用 UART 作为 console 后端 |
| `CONFIG_SHELL=y` | 启用 Zephyr Shell |
| `CONFIG_SHELL_BACKEND_SERIAL=y` | 启用串口 Shell 后端 |
| `CONFIG_SHELL_PROMPT_UART` | 设置串口 Shell 提示符 |

当前项目完整相关配置如下：

```conf
CONFIG_PRINTK=y
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_STDOUT_CONSOLE=y
CONFIG_GPIO=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_SHELL_PROMPT_UART="craner:~$ "
```

## 二、编译、烧录和进入 Shell

在项目根目录编译：

```powershell
.\build.ps1
```

烧录：

```powershell
.\flash.ps1
```

打开串口终端，参数设置为：

| 参数 | 值 |
| --- | --- |
| 波特率 | 115200 |
| 数据位 | 8 |
| 校验位 | None |
| 停止位 | 1 |
| 流控 | None |

复位板子后，终端中会看到启动打印和 Shell 提示符，类似：

```text
craner_encoder_hub started on craner_general_stm32h743vit6
printk is also routed to UART5 PB6/PB5
Type 'fw_time' in shell to show firmware build time
craner:~$
```

看到 `craner:~$` 就说明 Shell 已经启动，可以输入命令。

## 三、体验内置 Shell 指令

Zephyr 启用 Shell 后，会自动带一些内置命令。不同配置下可用命令数量可能不同。

### 1. 查看帮助

输入：

```text
help
```

常见输出会列出可用命令，例如：

```text
Built-in commands list:
  clear
  device
  help
  history
  kernel
  resize
  shell
```

### 2. 查看内核相关命令

输入：

```text
kernel
```

可以查看 `kernel` 命令组下有哪些子命令。

常用示例：

```text
kernel version
kernel uptime
kernel cycles
```

含义：

| 命令 | 说明 |
| --- | --- |
| `kernel version` | 查看 Zephyr 内核版本 |
| `kernel uptime` | 查看系统启动后的运行时间 |
| `kernel cycles` | 查看系统 cycle 计数 |

### 3. 查看设备列表

输入：

```text
device list
```

这个命令可以查看当前系统中已注册的 device。排查驱动是否初始化成功时很有用。

### 4. 查看历史命令

输入：

```text
history
```

可以查看当前 Shell 中输入过的历史命令。

### 5. 清屏

输入：

```text
clear
```

可以清理终端显示。

## 四、实现自定义 Shell 指令

本项目已经实现了一个自定义命令：

```text
fw_time
```

作用是输出固件编译时间。

源码位于：

```text
src/main.c
```

### 1. 包含 Shell 头文件

```c
#include <zephyr/shell/shell.h>
```

### 2. 实现命令处理函数

```c
static int cmd_fw_time(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Firmware build time: %s %s", __DATE__, __TIME__);

	return 0;
}
```

函数参数说明：

| 参数 | 说明 |
| --- | --- |
| `shell` | 当前 Shell 实例，用于输出结果 |
| `argc` | 命令参数数量 |
| `argv` | 命令参数内容 |

这里暂时没有使用参数，所以用：

```c
ARG_UNUSED(argc);
ARG_UNUSED(argv);
```

避免编译器产生未使用参数警告。

输出内容使用：

```c
shell_print(shell, "...");
```

不要在 Shell 命令处理函数里优先使用 `printf()`，因为 `shell_print()` 会把输出绑定到当前 Shell 实例，更适合命令响应。

### 3. 注册命令

```c
SHELL_CMD_REGISTER(fw_time, NULL, "Show firmware build date and time.", cmd_fw_time);
```

参数说明：

| 参数 | 当前值 | 说明 |
| --- | --- | --- |
| `syntax` | `fw_time` | 用户在终端输入的命令名 |
| `subcmd` | `NULL` | 子命令表；没有子命令时填 `NULL` |
| `help` | `"Show firmware build date and time."` | `help` 中显示的说明 |
| `handler` | `cmd_fw_time` | 命令处理函数 |

注册完成后，在串口终端输入：

```text
fw_time
```

会看到类似输出：

```text
Firmware build time: Jul  3 2026 23:01:12
```

## 五、带参数的自定义命令示例

如果命令需要参数，可以读取 `argc` 和 `argv`。

例如实现一个 `echo_arg` 命令，把用户输入的参数打印出来：

```c
static int cmd_echo_arg(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(shell, "Usage: echo_arg <text>");
		return -EINVAL;
	}

	shell_print(shell, "arg: %s", argv[1]);

	return 0;
}

SHELL_CMD_REGISTER(echo_arg, NULL, "Print the first argument.", cmd_echo_arg);
```

终端输入：

```text
echo_arg hello
```

输出：

```text
arg: hello
```

注意：

| 表达式 | 含义 |
| --- | --- |
| `argv[0]` | 命令本身，例如 `echo_arg` |
| `argv[1]` | 第一个参数，例如 `hello` |
| `argc` | 参数总数，包含命令本身 |

## 六、带子命令的命令组示例

当命令变多时，建议使用命令组。

例如创建一个 `fw` 命令组，下面放 `time` 子命令：

```c
static int cmd_fw_time(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Firmware build time: %s %s", __DATE__, __TIME__);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_fw,
	SHELL_CMD(time, NULL, "Show firmware build date and time.", cmd_fw_time),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(fw, &sub_fw, "Firmware commands.", NULL);
```

终端输入：

```text
fw time
```

输出：

```text
Firmware build time: Jul  3 2026 23:01:12
```

这种方式适合后续扩展：

```text
fw time
fw version
fw reboot
fw info
```

## 七、常见问题排查

### 1. 看不到 Shell 提示符

检查：

| 检查项 | 说明 |
| --- | --- |
| 是否启用 `CONFIG_SHELL=y` | 没启用 Shell 就不会启动交互 |
| 是否启用 `CONFIG_SHELL_BACKEND_SERIAL=y` | 没启用串口后端就不能从 UART 交互 |
| DTS 是否配置 `zephyr,shell-uart` | Shell 需要知道使用哪个 UART |
| UART 引脚和接线是否正确 | TX/RX 需要交叉连接 |
| 串口参数是否正确 | 本项目为 115200 8N1 |

### 2. 有打印，但不能输入命令

常见原因是只接了 TX，没有接 RX。

Shell 需要 PC 向板子发送字符，所以必须接：

```text
USB 转串口 TX -> 板子 UART5_RX / PB5
```

同时确认终端工具没有开启硬件流控。

### 3. 输入命令没有回显

检查 RX 接线、终端流控、串口号是否正确。

如果终端能看到启动日志，但输入没有反应，重点检查：

```text
USB 转串口 TX -> PB5
GND -> GND
```

### 4. 自定义命令编译报错

检查是否包含头文件：

```c
#include <zephyr/shell/shell.h>
```

检查命令处理函数签名是否正确：

```c
static int cmd_xxx(const struct shell *shell, size_t argc, char **argv)
```

检查是否启用了：

```conf
CONFIG_SHELL=y
```

### 5. 命令输出建议

在 Shell 命令中推荐使用：

```c
shell_print(shell, "normal message");
shell_error(shell, "error message");
shell_warn(shell, "warning message");
```

这样输出会更符合 Shell 框架的行为。

## 八、新增 Shell 命令 checklist

以后新增自定义命令时，可以按这个顺序做：

1. 确认 `prj.conf` 已启用 `CONFIG_SHELL=y`。
2. 在源码中包含 `<zephyr/shell/shell.h>`。
3. 编写命令处理函数。
4. 使用 `shell_print()` 输出命令结果。
5. 使用 `SHELL_CMD_REGISTER()` 注册命令。
6. 编译并烧录。
7. 打开 UART5 串口终端。
8. 输入 `help` 确认命令是否出现在列表中。
9. 输入自定义命令验证输出。

完成以上步骤后，就可以通过串口终端和设备进行 Shell 交互。
