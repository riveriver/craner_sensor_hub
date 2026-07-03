# craner_encoder_hub

Independent Zephyr application for the `mini_stm32h743` board.

## Board

| Item | Value |
| --- | --- |
| Zephyr board id | `mini_stm32h743` |
| Board | WeAct Studio MiniSTM32H743 Core Board |
| MCU | STM32H743VIT6 |
| Zephyr board files | `zephyrproject/zephyr/boards/weact/mini_stm32h743/` |

## Build

From this directory:

```powershell
.\build.ps1
```

Equivalent manual command:

```powershell
$env:ZEPHYR_BASE = "C:\Users\river\dev\zephyr_ws\zephyrproject\zephyr"
python -m west build -b mini_stm32h743 . -d build\mini_stm32h743
```

## Flash

Connect ST-LINK to the board, then flash the existing build:

```powershell
.\flash.ps1
```

Equivalent manual command:

```powershell
python -m west flash -d build\mini_stm32h743 --runner stm32cubeprogrammer
```
