# craner_sensor_hub

用于 `craner_general_stm32h743vit6` 板子的独立 Zephyr 应用。

## 板卡信息

| 项目 | 值 |
| --- | --- |
| Zephyr 板卡 ID | `craner_general_stm32h743vit6` |
| 板卡 | Craner General STM32H743VIT6 PCB V1.1.0 |
| PCB 硬件版本 | `1.1.0` |
| 主控 | STM32H743VIT6 |
| 控制台串口 | UART5，PB6 TX / PB5 RX，115200 波特率 |
| Zephyr 板级文件 | `boards/craner/craner_general_stm32h743vit6/` |

## 编译

首次拉取仓库后初始化共享开发脚本子模块：

```powershell
git submodule update --init --recursive
```

在当前目录执行：

```powershell
.\tool\zephyr-dev-workflow\script\build.ps1
```

等价的手动命令：

```powershell
$env:ZEPHYR_BASE = "C:\Users\river\dev\zephyr_ws\zephyrproject\zephyr"
python -m west build -b craner_general_stm32h743vit6 . -d build\craner_general_stm32h743vit6
```

## 烧录

将 ST-LINK 连接到板子后，烧录已有构建产物：

```powershell
.\tool\zephyr-dev-workflow\script\flash.ps1
```

等价的手动命令：

```powershell
python -m west flash -d build\craner_general_stm32h743vit6 --runner stm32cubeprogrammer
```
