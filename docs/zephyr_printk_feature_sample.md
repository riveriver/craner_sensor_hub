# Zephyr printk/printf 示例：启动日志输出

## 1. 示例实现了什么

本示例演示如何在 Zephyr 应用启动后，通过板卡控制台串口输出 `printf()` 和 `printk()` 日志。

当前启动入口在 `src/main.c`：

```c
int main(void)
{
	printf("craner_encoder_hub started on %s\n", CONFIG_BOARD);
	printk("printk is routed to the board console UART\n");
	printk("Type 'fw_time' in shell to show firmware build time\n");

	while (1) {
		k_sleep(K_SECONDS(1));
	}
}
```

两块板的 console 串口：

| 板卡 | Console 串口 | 引脚 |
| --- | --- | --- |
| `craner_general_stm32h743vit6` | UART5 | PB6 TX / PB5 RX |
| `mp_rs485x4_stm32h743vit6` | USART1 | PA9 TX / PA10 RX |

## 2. 怎么使用

编译：

```powershell
.\build.ps1
```

烧录：

```powershell
.\flash.ps1
```

打开对应串口，参数为 `115200 8N1`。期望看到：

```text
craner_encoder_hub started on mp_rs485x4_stm32h743vit6
printk is routed to the board console UART
Type 'fw_time' in shell to show firmware build time
```

如果编译 Craner 板：

```powershell
.\build.ps1 -Board craner_general_stm32h743vit6
```

第一行中的 `CONFIG_BOARD` 会变成：

```text
craner_encoder_hub started on craner_general_stm32h743vit6
```

## 3. 前置条件

需要满足：

| 项目 | 说明 |
| --- | --- |
| 调试串口连线 | TX/RX 交叉，GND 共地 |
| 串口参数 | `115200 8N1` |
| DTS | `zephyr,console` 指向正确 UART |
| Kconfig | 启用 console、UART console 和 stdout console |

## 4. 设备树：硬件描述

`printk()` 和 `printf()` 最终都会从 console 输出。console 设备由 DTS 的 `chosen` 节点指定。

Craner 板：

```dts
chosen {
	zephyr,console = &uart5;
};

&uart5 {
	pinctrl-0 = <&uart5_tx_pb6 &uart5_rx_pb5>;
	pinctrl-names = "default";
	current-speed = <115200>;
	status = "okay";
};
```

MP 板：

```dts
chosen {
	zephyr,console = &usart1;
};

&usart1 {
	pinctrl-0 = <&usart1_tx_pa9 &usart1_rx_pa10>;
	pinctrl-names = "default";
	current-speed = <115200>;
	status = "okay";
};
```

`status = "okay"` 表示启用这个 UART。`pinctrl-0` 描述 UART 信号映射到哪些 MCU 引脚。

## 5. Kconfig/prj.conf：软件配置

相关配置：

```conf
CONFIG_PRINTK=y
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_STDOUT_CONSOLE=y
```

含义：

| 配置 | 作用 |
| --- | --- |
| `CONFIG_PRINTK=y` | 启用 `printk()` |
| `CONFIG_SERIAL=y` | 启用串口驱动 |
| `CONFIG_CONSOLE=y` | 启用 console 子系统 |
| `CONFIG_UART_CONSOLE=y` | 让 console 使用 UART |
| `CONFIG_STDOUT_CONSOLE=y` | 让标准输出 `printf()` 走 console |

`printk()` 是 Zephyr 的轻量输出接口。`printf()` 来自 C 标准库，启用 `CONFIG_STDOUT_CONSOLE` 后会重定向到 console。

## 6. 业务/应用代码

`src/main.c` 保持很薄，只做三件事：

1. 输出启动信息。
2. 提示 Shell 命令。
3. 进入低频 sleep 循环，避免 `main()` 退出。

`CONFIG_BOARD` 是 Zephyr 自动生成的 Kconfig 字符串，等于当前编译板卡名。

## 7. 如何扩展

如果要输出更多启动信息，可以继续在 `main()` 中打印简短信息：

```c
printk("Firmware boot OK\n");
printf("Board: %s\n", CONFIG_BOARD);
```

如果输出会变多，建议使用 Zephyr logging 子系统，而不是把复杂业务日志都放在 `main()` 里。

## 8. 常见问题排查

| 现象 | 检查项 |
| --- | --- |
| 串口完全无输出 | 检查 `zephyr,console`、UART pinctrl、串口线 |
| `printk()` 有输出但 `printf()` 没有 | 检查 `CONFIG_STDOUT_CONSOLE=y` |
| 输出乱码 | 检查 `current-speed` 和串口工具波特率 |
| 编译后板名不对 | 检查 `.\build.ps1 -Board <board>` 参数 |

生成文件检查：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\zephyr.dts -Pattern "zephyr,console"
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\.config -Pattern "CONFIG_PRINTK|CONFIG_UART_CONSOLE|CONFIG_STDOUT_CONSOLE"
```
