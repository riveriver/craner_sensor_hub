# Zephyr 中将 printf 重定向到串口

本文面向刚接触本项目的同事，说明如何在 Zephyr 中把 `printf()` 输出重定向到指定 UART，并在电脑终端中看到打印信息。

本项目当前使用的板子是 `craner_general_board_v110`，主控是 `STM32H743VIT6`，控制台串口配置为：

| 项目 | 配置 |
| --- | --- |
| 串口 | UART5 |
| TX | PB6 |
| RX | PB5 |
| 波特率 | 115200 |

## 基本原理

Zephyr 中常见的打印接口有两类：

| 接口 | 说明 |
| --- | --- |
| `printk()` | Zephyr 内核提供的轻量打印接口 |
| `printf()` | C 标准库打印接口 |

要让 `printf()` 从串口输出，需要完成三件事：

1. 在 devicetree 中指定哪个 UART 是系统控制台。
2. 在 Kconfig 中启用 UART console。
3. 在 Kconfig 中启用 stdout console，让 `printf()` 输出接到 console。

简单理解就是：

```text
printf()
  -> stdout
  -> Zephyr console
  -> UART driver
  -> UART5 TX 引脚
  -> USB 转串口模块
  -> PC 终端
```

## 第一步：在 DTS 中指定控制台 UART

板级 devicetree 文件位于：

```text
boards/craner/craner_general_board_v110/craner_general_board_v110.dts
```

在根节点的 `chosen` 中指定控制台：

```dts
/ {
	model = "Craner General Board V1.10";
	compatible = "craner,craner-general-board-v110";

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
zephyr,console = &uart5;
```

它告诉 Zephyr：系统 console 使用 `uart5`。

如果后续需要把打印改到别的串口，例如 `usart1`，就把这里改成：

```dts
zephyr,console = &usart1;
```

同时还要配置对应串口节点和引脚。

## 第二步：配置 UART 引脚和波特率

同一个 DTS 文件中，需要启用 `uart5` 节点：

```dts
&uart5 {
	pinctrl-0 = <&uart5_tx_pb6 &uart5_rx_pb5>;
	pinctrl-names = "default";
	current-speed = <115200>;
	status = "okay";
};
```

这段配置的含义是：

| 配置 | 含义 |
| --- | --- |
| `pinctrl-0` | UART5 使用的引脚复用配置 |
| `uart5_tx_pb6` | UART5 TX 使用 PB6 |
| `uart5_rx_pb5` | UART5 RX 使用 PB5 |
| `current-speed` | 默认波特率为 115200 |
| `status = "okay"` | 启用 UART5 外设 |

注意：电脑 USB 转串口模块接线时，需要交叉连接：

```text
板子 PB6 / UART5_TX  ->  USB 转串口 RX
板子 PB5 / UART5_RX  ->  USB 转串口 TX
板子 GND             ->  USB 转串口 GND
```

如果只看打印，不需要从电脑向板子输入命令，理论上只接 `PB6 -> RX` 和 `GND` 也能看到输出。

## 第三步：启用 Zephyr 配置

项目配置文件位于：

```text
prj.conf
```

需要启用以下配置：

```conf
CONFIG_PRINTK=y
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_STDOUT_CONSOLE=y
```

各项含义如下：

| 配置 | 作用 |
| --- | --- |
| `CONFIG_PRINTK=y` | 启用 `printk()` |
| `CONFIG_SERIAL=y` | 启用串口驱动 |
| `CONFIG_CONSOLE=y` | 启用 Zephyr console |
| `CONFIG_UART_CONSOLE=y` | 使用 UART 作为 console 后端 |
| `CONFIG_STDOUT_CONSOLE=y` | 将 C 标准输出接到 console，使 `printf()` 可用 |

其中和 `printf()` 最直接相关的是：

```conf
CONFIG_STDOUT_CONSOLE=y
```

如果只启用了 `UART_CONSOLE`，通常 `printk()` 能输出，但 `printf()` 不一定会从串口出来。

## 第四步：在代码中使用 printf

应用入口位于：

```text
src/main.c
```

示例代码：

```c
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	printf("craner_encoder_hub started on craner_general_board_v110\n");
	printk("printk is also routed to UART5 PB6/PB5\n");

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
```

使用 `printf()` 时需要包含：

```c
#include <stdio.h>
```

建议每条日志末尾加 `\n`：

```c
printf("hello uart5\n");
```

这样终端显示更清晰，也更容易触发缓冲刷新。

## 第五步：编译和烧录

在项目根目录执行：

```powershell
.\build.ps1
```

编译成功后，烧录：

```powershell
.\flash.ps1
```

当前脚本默认使用的板子是：

```text
craner_general_board_v110
```

构建输出目录是：

```text
build/craner_general_board_v110
```

## 第六步：打开串口终端

使用任意串口工具都可以，例如 MobaXterm、PuTTY、Tera Term、串口助手等。

推荐串口参数：

| 参数 | 值 |
| --- | --- |
| 波特率 | 115200 |
| 数据位 | 8 |
| 校验位 | None |
| 停止位 | 1 |
| 流控 | None |

打开终端后，复位板子，应该能看到类似输出：

```text
craner_encoder_hub started on craner_general_board_v110
printk is also routed to UART5 PB6/PB5
```

## 如何确认配置是否生效

编译后可以检查生成文件：

```text
build/craner_general_board_v110/zephyr/zephyr.dts
build/craner_general_board_v110/zephyr/.config
```

在 `zephyr.dts` 中应该能看到：

```dts
zephyr,console = &uart5;
```

以及：

```dts
uart5: serial@40005000 {
	pinctrl-0 = < &uart5_tx_pb6 &uart5_rx_pb5 >;
	current-speed = < 0x1c200 >;
	status = "okay";
};
```

其中 `0x1c200` 是十六进制，等于十进制 `115200`。

在 `.config` 中应该能看到：

```conf
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_STDOUT_CONSOLE=y
```

## 常见问题排查

### 1. 终端完全没有输出

优先检查：

| 检查项 | 说明 |
| --- | --- |
| GND 是否连接 | 板子 GND 必须和 USB 转串口 GND 共地 |
| TX/RX 是否接反 | 板子 TX 应接 USB 转串口 RX |
| 波特率是否一致 | 本项目默认 115200 |
| 串口号是否选对 | Windows 设备管理器中确认 COM 口 |
| 固件是否烧录成功 | 确认 `west flash` 没有报错 |

### 2. `printk()` 有输出，但 `printf()` 没输出

检查是否启用了：

```conf
CONFIG_STDOUT_CONSOLE=y
```

同时建议 `printf()` 后面加换行：

```c
printf("hello\n");
```

### 3. 输出乱码

通常是波特率不一致。确认 DTS 中：

```dts
current-speed = <115200>;
```

终端工具也设置为 `115200 8N1`。

### 4. 编译提示找不到板子

确认 `CMakeLists.txt` 中包含：

```cmake
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
```

这行配置的作用是告诉 Zephyr：当前应用目录中也有自定义 board。

### 5. 修改 DTS 后没有生效

可以清理后重新编译：

```powershell
Remove-Item -Recurse -Force .\build\craner_general_board_v110
.\build.ps1
```

也可以直接使用 pristine build：

```powershell
python -m west build -p always -b craner_general_board_v110 . -d build\craner_general_board_v110
```

## 新板子迁移 checklist

如果以后要给另一块公司板子做 printf 串口输出，按下面顺序检查：

1. 确认原理图中的 UART 编号，例如 UART5、USART1。
2. 确认 TX/RX 对应的 MCU 引脚。
3. 在板级 DTS 中配置 `zephyr,console = &xxx`。
4. 在同一个 DTS 中启用对应 UART 节点。
5. 配置 `pinctrl-0`、`current-speed`、`status = "okay"`。
6. 在 `prj.conf` 中启用 `CONFIG_UART_CONSOLE=y` 和 `CONFIG_STDOUT_CONSOLE=y`。
7. 在代码中包含 `<stdio.h>` 并使用 `printf()`。
8. 编译、烧录、打开串口终端验证输出。

完成以上步骤后，`printf()` 就会通过指定串口输出到 PC 终端。
