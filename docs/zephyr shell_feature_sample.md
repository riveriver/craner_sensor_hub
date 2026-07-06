# Zephyr Shell 示例：串口交互和自定义命令

## 1. 示例实现了什么

本示例在当前工程中启用 Zephyr Shell，并把 Shell 绑定到板卡的调试串口。固件启动后，用户可以通过串口终端看到 `craner:~$` 提示符，并执行内置 Shell 命令和项目自定义命令。

当前自定义命令为：

| 命令 | 作用 | 源文件 |
| --- | --- | --- |
| `fw_time` | 打印固件编译日期和时间 | `src/shell_app.c` |

两块板的 Shell 串口不同：

| 板卡 | Shell 串口 | 引脚 | 波特率 |
| --- | --- | --- | --- |
| `craner_general_stm32h743vit6` | UART5 | PB6 TX / PB5 RX | 115200 |
| `mp_rs485x4_stm32h743vit6` | USART1 | PA9 TX / PA10 RX | 115200 |

## 2. 怎么使用

编译默认板卡：

```powershell
.\build.ps1
```

编译 Craner 板卡：

```powershell
.\build.ps1 -Board craner_general_stm32h743vit6
```

烧录后打开对应调试串口，参数为 `115200 8N1`，无流控。启动后应看到类似输出：

```text
craner_encoder_hub started on mp_rs485x4_stm32h743vit6
printk is routed to the board console UART
Type 'fw_time' in shell to show firmware build time
craner:~$
```

输入自定义命令：

```text
fw_time
```

期望输出类似：

```text
Firmware build time: Jul  6 2026 20:30:00
```

也可以试 Zephyr 内置命令：

```text
help
kernel version
device list
log status
net iface
```

## 3. 前置条件

需要准备：

| 项目 | 说明 |
| --- | --- |
| 串口工具 | MobaXterm、PuTTY、Tera Term、串口助手均可 |
| 串口参数 | `115200 8N1`，无硬件流控 |
| 板卡 DTS | 必须设置 `zephyr,shell-uart` |
| Kconfig | 必须启用 Shell 和串口后端 |

注意 TX/RX 交叉连接：板卡 TX 接 USB-TTL RX，板卡 RX 接 USB-TTL TX，GND 共地。

## 4. 设备树：硬件描述

Shell 不是自己选择串口的，它读取 DTS 的 `chosen` 节点。

Craner 板：

```dts
chosen {
	zephyr,console = &uart5;
	zephyr,shell-uart = &uart5;
};
```

`zephyr,console` 决定 `printk()` 和控制台输出使用哪个设备，`zephyr,shell-uart` 决定 Shell 后端绑定哪个 UART。

UART5 节点描述硬件引脚：

```dts
&uart5 {
	pinctrl-0 = <&uart5_tx_pb6 &uart5_rx_pb5>;
	pinctrl-names = "default";
	current-speed = <115200>;
	status = "okay";
};
```

这里的含义是：启用 UART5，把 TX 映射到 PB6，把 RX 映射到 PB5，默认波特率是 115200。

MP 板同理，只是 Shell UART 换成 USART1：

```dts
chosen {
	zephyr,console = &usart1;
	zephyr,shell-uart = &usart1;
};

&usart1 {
	pinctrl-0 = <&usart1_tx_pa9 &usart1_rx_pa10>;
	pinctrl-names = "default";
	current-speed = <115200>;
	status = "okay";
};
```

## 5. Kconfig/prj.conf：软件配置

Shell 相关配置在 `prj.conf`：

```conf
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_STDOUT_CONSOLE=y

CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_SHELL_PROMPT_UART="craner:~$ "
```

每个配置的作用：

| 配置 | 作用 |
| --- | --- |
| `CONFIG_SERIAL=y` | 启用串口驱动基础能力 |
| `CONFIG_CONSOLE=y` | 启用 Zephyr console 子系统 |
| `CONFIG_UART_CONSOLE=y` | 允许 console 使用 UART |
| `CONFIG_STDOUT_CONSOLE=y` | 让 `printf()` 输出走 console |
| `CONFIG_SHELL=y` | 启用 Zephyr Shell |
| `CONFIG_SHELL_BACKEND_SERIAL=y` | 使用串口作为 Shell 输入输出后端 |
| `CONFIG_SHELL_PROMPT_UART` | 设置 Shell 提示符文本 |

## 6. 业务/应用代码

自定义命令位于 `src/shell_app.c`：

```c
#include <zephyr/shell/shell.h>

static int cmd_fw_time(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Firmware build time: %s %s", __DATE__, __TIME__);

	return 0;
}

SHELL_CMD_REGISTER(fw_time, NULL, "Show firmware build date and time.", cmd_fw_time);
```

实现步骤是：

1. 引入 `zephyr/shell/shell.h`。
2. 写一个命令处理函数。
3. 在函数里用 `shell_print()` 输出到当前 Shell。
4. 用 `SHELL_CMD_REGISTER()` 注册命令名。
5. 在 `CMakeLists.txt` 中编译 `src/shell_app.c`。

`CMakeLists.txt` 中对应配置：

```cmake
target_sources(app PRIVATE
	src/main.c
	src/shell_app.c
)
```

## 7. 如何扩展

新增命令时，可以复制 `fw_time` 的模式：

```c
static int cmd_board_name(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Board: %s", CONFIG_BOARD);
	return 0;
}

SHELL_CMD_REGISTER(board_name, NULL, "Show board name.", cmd_board_name);
```

如果要做一组命令，例如 `app status`、`app reset`、`app version`，应使用 `SHELL_STATIC_SUBCMD_SET_CREATE()` 创建子命令集合。

## 8. 常见问题排查

| 现象 | 检查项 |
| --- | --- |
| 串口无输出 | 检查 `chosen` 是否指向正确 UART，TX/RX 是否接反 |
| 有启动日志但没有 Shell 提示符 | 检查 `CONFIG_SHELL=y` 和 `CONFIG_SHELL_BACKEND_SERIAL=y` |
| `fw_time` 不存在 | 检查 `src/shell_app.c` 是否加入 `CMakeLists.txt` |
| 输入乱码 | 检查波特率是否为 115200 |
| `printf()` 无输出 | 检查 `CONFIG_STDOUT_CONSOLE=y` |

生成后也可以检查：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\zephyr.dts -Pattern "zephyr,shell-uart|zephyr,console"
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\.config -Pattern "CONFIG_SHELL|CONFIG_SHELL_BACKEND_SERIAL"
```
