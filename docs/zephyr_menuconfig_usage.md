# Zephyr menuconfig 使用教程

## 这个文档解决什么问题

`menuconfig` 是 Zephyr 的交互式 Kconfig 配置界面。它可以用来查看、搜索、临时修改 `CONFIG_*` 配置项。

在本项目里，`menuconfig` 最常用来检查和调试这些配置：

| 配置类型 | 示例 |
| --- | --- |
| 应用业务线程 | `CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD` |
| 网络功能 | `CONFIG_NETWORKING`、`CONFIG_NET_TCP` |
| Modbus 功能 | `CONFIG_MODBUS`、`CONFIG_MODBUS_RAW_ADU` |
| Shell/日志 | `CONFIG_SHELL`、`CONFIG_LOG` |
| MCUboot | `CONFIG_BOOT_SWAP_USING_SCRATCH`、`CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256` |

`menuconfig` 很适合用来探索配置，但它不是长期保存配置的最佳位置。最终要保留的配置，应该写回 `prj.conf`、`sysbuild.conf` 或 MCUboot 的配置文件中。

## 前置条件

先至少构建过一次工程，因为 `menuconfig` 依赖 build 目录里的 Kconfig 解析结果：

```powershell
.\build.ps1 -Board mp_rs485x4_stm32h743vit6
```

当前工程启用了 sysbuild，所以一次构建会生成多个子工程：

```text
build\mp_rs485x4_stm32h743vit6\
├── craner_encoder_hub\                 应用 slot0
├── mcuboot\                            MCUboot bootloader
└── zephyr\                             sysbuild 自己的配置
```

平时我们主要改应用配置，所以最常打开的是：

```text
build\mp_rs485x4_stm32h743vit6\craner_encoder_hub
```

## 打开应用的 menuconfig

在项目根目录运行：

```powershell
python -m west build -t menuconfig -d build\mp_rs485x4_stm32h743vit6\craner_encoder_hub
```

如果要打开另一块板：

```powershell
.\build.ps1 -Board craner_general_stm32h743vit6
python -m west build -t menuconfig -d build\craner_general_stm32h743vit6\craner_encoder_hub
```

## 常用按键

| 按键 | 作用 |
| --- | --- |
| `↑` / `↓` | 上下移动 |
| `Enter` | 进入菜单或确认 |
| `Space` | 切换 bool 配置的 `y` / `n` |
| `/` | 搜索配置项 |
| `Esc` `Esc` | 返回上一级 |
| `S` | 保存 |
| `Q` | 退出 |
| `?` | 查看当前配置帮助 |

搜索时可以输入不带 `CONFIG_` 的名字。例如要找线程开关，按 `/` 后输入：

```text
CRANER_ENABLE
```

## 修改业务线程开关

本项目的线程开关在应用 Kconfig 中定义。进入应用 `menuconfig` 后，可以搜索：

```text
CRANER_ENABLE
```

常见配置如下：

```text
CONFIG_CRANER_ENABLE_HEARTBEAT_LED_THREAD
CONFIG_CRANER_ENABLE_MODBUS_TCP_SERVER_THREAD
CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD
CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD
CONFIG_CRANER_ENABLE_READ_HOISTING_ENCODER_THREAD
CONFIG_CRANER_ENABLE_LOG_SENSOR_THREAD
CONFIG_CRANER_ENABLE_LOG_COMM_THREAD
```

例如你想关闭变幅编码器线程：

1. 按 `/`。
2. 输入 `CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD`。
3. 进入搜索结果。
4. 按 `Space` 改成未选中。
5. 按 `S` 保存。
6. 按 `Q` 退出。

保存后检查：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\craner_encoder_hub\zephyr\.config -Pattern "CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD"
```

如果关闭成功，应该看到类似：

```text
# CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD is not set
```

## 保存到哪里

`menuconfig` 保存的是当前 build 目录里的：

```text
build\mp_rs485x4_stm32h743vit6\craner_encoder_hub\zephyr\.config
```

这个文件是构建产物，不建议手动维护，也不应该作为长期配置来源。

本项目的 `build.ps1` 使用 pristine build，每次重新构建都会重新生成 build 目录，所以 `menuconfig` 里的修改可能会被清掉。

长期有效的配置应该写回项目根目录：

```text
prj.conf
```

例如：

```conf
CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD=n
```

推荐工作流是：

1. 用 `menuconfig` 搜索和试验配置。
2. 确认配置能解决问题。
3. 把配置写回 `prj.conf`。
4. 重新构建验证。

## 查看修改差异

如果你在 `menuconfig` 里改了一些配置，想知道和默认配置相比改了什么，可以运行：

```powershell
python -m west build -t diffconfig -d build\mp_rs485x4_stm32h743vit6\craner_encoder_hub
```

这个命令会输出当前 `.config` 中相对默认配置的差异。你可以把需要长期保留的行复制到 `prj.conf`。

## 打开 MCUboot 的 menuconfig

如果要查看 MCUboot 自己的配置，例如签名算法、swap using scratch，可以打开 MCUboot 子工程：

```powershell
python -m west build -t menuconfig -d build\mp_rs485x4_stm32h743vit6\mcuboot
```

常见搜索项：

```text
BOOT_SWAP_USING_SCRATCH
BOOT_SIGNATURE_TYPE_ECDSA_P256
```

注意：MCUboot 的配置不是写进应用 `prj.conf`。sysbuild 相关配置一般写在：

```text
sysbuild.conf
```

如果后续要给 MCUboot 增加更细的配置，通常会增加 MCUboot 专用配置文件，再由 sysbuild 引入。

## 打开 sysbuild 的 menuconfig

sysbuild 自己也有一层配置，例如是否启用 MCUboot、MCUboot 模式等。

打开方式：

```powershell
python -m west build -t menuconfig -d build\mp_rs485x4_stm32h743vit6
```

常见搜索项：

```text
SB_CONFIG_BOOTLOADER_MCUBOOT
SB_CONFIG_MCUBOOT_MODE_SWAP_SCRATCH
SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256
```

这些配置的长期来源是：

```text
sysbuild.conf
```

## Kconfig 和 prj.conf 的关系

`Kconfig` 定义“有哪些配置项”，`prj.conf` 设置“这些配置项取什么值”。

例如项目根目录 `Kconfig` 中定义：

```kconfig
config CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD
	bool "Enable slewing encoder Modbus RTU thread"
	default y
	depends on MODBUS
```

然后 `prj.conf` 中设置：

```conf
CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD=y
```

构建时 Zephyr 会把它们合并，生成最终配置：

```text
build\mp_rs485x4_stm32h743vit6\craner_encoder_hub\zephyr\.config
```

C 代码和 CMake 都可以使用最终生成的配置：

```c
#if defined(CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD)
K_THREAD_DEFINE(...);
#endif
```

```cmake
target_sources_ifdef(CONFIG_CRANER_ENABLE_HEARTBEAT_LED_THREAD app PRIVATE
	src/heartbeat_led_app.c
)
```

## 常见问题

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| `menuconfig` 打不开 | build 目录不存在或没有先构建 | 先运行 `.\build.ps1 -Board mp_rs485x4_stm32h743vit6` |
| 找不到 `CRANER_ENABLE_*` | 打开的不是应用 build 目录 | 使用 `-d build\...\craner_encoder_hub` |
| 保存后下次构建又恢复了 | 修改只保存在 `.config` | 把配置写回 `prj.conf` |
| 某个选项不能打开 | 依赖条件不满足 | 按 `?` 查看依赖，例如是否缺少 `CONFIG_MODBUS` |
| 出现 `was assigned y but got n` | `prj.conf` 强制打开，但依赖不满足 | 先启用依赖项，或关闭该配置 |
| Windows 终端显示异常 | 终端不兼容 curses 界面 | 使用 Windows Terminal 或尝试 `guiconfig` |

如果 `menuconfig` 界面在 Windows 上显示不正常，可以尝试：

```powershell
python -m west build -t guiconfig -d build\mp_rs485x4_stm32h743vit6\craner_encoder_hub
```

`guiconfig` 是否可用取决于本机 Python/Qt 相关环境，优先推荐使用 `menuconfig`。
