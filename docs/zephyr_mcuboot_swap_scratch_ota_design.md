# Zephyr MCUboot Swap Using Scratch OTA 方案

## 1. This example implements

本工程 OTA 方案使用 Zephyr / MCUboot 自带的 `Swap using scratch` 模式。第一版 OTA 传输方式使用 Zephyr MCUmgr SMP over UDP，PC 端使用 `mcumgr` 命令行工具通过以太网上传固件。

应用程序始终链接并运行在 `slot0_partition`。升级时，新的 signed image 先写入 `slot1_partition`，然后应用调用 MCUboot API 把 secondary image 标记为 test upgrade。设备重启后，MCUboot 使用 `scratch_partition` 作为临时搬运区，把 `slot1_partition` 和 `slot0_partition` 交换。新固件启动后必须确认，未确认时再次重启会自动回滚到旧固件。

这套方案不再使用项目自定义 `boot_state_partition`，也不再使用 direct-XIP A/B variant、`FIND_NEXT_SLOT_HOOKS` 或两套不同链接地址的固件。应用只生成一套按 `slot0_partition` 链接的 signed image，升级镜像由 MCUmgr 写入 `slot1_partition`。

当前第一版不实现网页上传。网页上传可以作为后续扩展，但底层仍建议复用同一套 `slot1_partition + boot_request_upgrade(BOOT_UPGRADE_TEST) + confirm/revert` 状态机。

## 2. How to use it

### 2.1 编译

```powershell
.\build.ps1 -Board mp_rs485x4_stm32h743vit6
```

编译完成后会生成：

```text
build\<board>\craner_encoder_hub\zephyr\zephyr.signed.bin
build\<board>\craner_encoder_hub\zephyr\zephyr.signed.hex
build\<board>\craner_encoder_hub\zephyr\zephyr.signed.confirmed.bin
build\<board>\craner_encoder_hub\zephyr\zephyr.signed.confirmed.hex

build\<board>\ota_images\app_update_signed.bin
build\<board>\ota_images\app_update_signed.hex
build\<board>\ota_images\app_initial_confirmed.bin
build\<board>\ota_images\app_initial_confirmed.hex
```

OTA 上传使用：

```text
app_update_signed.bin
```

首次 SWD 烧录应用建议使用：

```text
app_initial_confirmed.hex
```

`confirmed` 镜像表示初始固件已经被标记为稳定版本，避免第一次启动后被 MCUboot 当成未确认测试镜像。

### 2.2 初次烧录

烧录 MCUboot 和 confirmed 应用：

```powershell
.\flash.ps1 -Board mp_rs485x4_stm32h743vit6 -Target All
```

只烧录 bootloader：

```powershell
.\flash.ps1 -Board mp_rs485x4_stm32h743vit6 -Target Bootloader
```

只烧录 confirmed 应用：

```powershell
.\flash.ps1 -Board mp_rs485x4_stm32h743vit6 -Target App
```

### 2.3 Shell 验证 OTA 状态

查看 MCUboot 状态：

```text
ota show
```

把 secondary slot 标记为试运行升级：

```text
ota test
ota reboot
```

新固件启动后，如果运行正常，确认当前固件：

```text
ota confirm
```

如果新固件未确认，应用会在 `CONFIG_CRANER_OTA_TEST_TIMEOUT_S` 秒后主动重启。MCUboot 发现当前 image 未确认，会自动回滚到旧固件。

### 2.4 通过以太网执行 OTA

确认 MCU 已启动以太网，IP 为当前工程静态地址：

```text
192.168.18.32
```

PC 端先查看 MCUboot image 状态：

```powershell
mcumgr --conntype udp --connstring 192.168.18.32:1337 image list
```

上传新固件：

```powershell
mcumgr --conntype udp --connstring 192.168.18.32:1337 image upload build\mp_rs485x4_stm32h743vit6\ota_images\app_update_signed.bin
```

上传完成后再次查看 image 列表，记录 `slot=1` 那个镜像的 `hash`：

```powershell
mcumgr --conntype udp --connstring 192.168.18.32:1337 image list
```

把新镜像标记为 test upgrade：

```powershell
mcumgr --conntype udp --connstring 192.168.18.32:1337 image test <slot1_hash>
```

重启进入新固件：

```powershell
mcumgr --conntype udp --connstring 192.168.18.32:1337 reset
```

新固件启动后，确认业务功能正常，再确认固件：

```powershell
mcumgr --conntype udp --connstring 192.168.18.32:1337 image confirm
```

也可以在串口 shell 中确认：

```text
ota confirm
```

如果新固件没有确认，应用会在 `CONFIG_CRANER_OTA_TEST_TIMEOUT_S` 秒后主动重启，MCUboot 自动回滚到旧固件。

## 3. Prerequisites

需要满足以下条件：

```text
STM32H743 internal flash 2MB
MCUboot bootloader
slot0_partition
slot1_partition
scratch_partition
Zephyr flash_map / img_manager / mcuboot_img_manager
```

当前第一版 OTA 使用 MCUmgr SMP over UDP 作为以太网传输入口。MCU 监听 UDP `1337` 端口，PC 使用 `mcumgr` 上传 `app_update_signed.bin` 到 `slot1_partition`。

PC 端需要安装 `mcumgr`。Windows 推荐先安装 Go：

```powershell
winget install GoLang.Go
```

重新打开 PowerShell 后安装 `mcumgr`：

```powershell
go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest
```

确认 `mcumgr.exe` 所在目录已经在 `PATH` 中。默认路径通常是：

```text
C:\Users\<用户名>\go\bin
```

临时加入当前 PowerShell：

```powershell
$env:Path += ";$env:USERPROFILE\go\bin"
```

验证：

```powershell
mcumgr version
```

典型 PC 命令如下。`image test` 的 `<slot1_hash>` 来自 `image list` 输出中 `slot=1` 的 hash：

```powershell
mcumgr --conntype udp --connstring 192.168.18.32:1337 image list
mcumgr --conntype udp --connstring 192.168.18.32:1337 image upload build\mp_rs485x4_stm32h743vit6\ota_images\app_update_signed.bin
mcumgr --conntype udp --connstring 192.168.18.32:1337 image test <slot1_hash>
mcumgr --conntype udp --connstring 192.168.18.32:1337 reset
mcumgr --conntype udp --connstring 192.168.18.32:1337 image confirm
```

也可以上传完成后在串口 shell 中执行：

```text
ota test
ota reboot
```

第一版 UDP SMP 未启用 DTLS/鉴权，不要暴露到不受控网络。

## 4. Devicetree: hardware description

内部 Flash 分区如下：

```text
STM32H743 internal flash 2MB

0x08000000 ~ 0x0801FFFF   boot_partition        128KB
0x08020000 ~ 0x080DFFFF   slot0_partition       768KB
0x080E0000 ~ 0x0819FFFF   slot1_partition       768KB
0x081A0000 ~ 0x081BFFFF   scratch_partition     128KB
0x081C0000 ~ 0x081FFFFF   app_storage_partition 256KB
```

DTS 中关键配置：

```dts
chosen {
	zephyr,code-partition = &slot0_partition;
};

&flash0 {
	partitions {
		compatible = "fixed-partitions";

		boot_partition: partition@0 {
			label = "mcuboot";
			reg = <0x00000000 0x00020000>;
		};

		slot0_partition: partition@20000 {
			label = "image-0";
			reg = <0x00020000 0x000c0000>;
		};

		slot1_partition: partition@e0000 {
			label = "image-1";
			reg = <0x000e0000 0x000c0000>;
		};

		scratch_partition: partition@1a0000 {
			label = "image-scratch";
			reg = <0x001a0000 0x00020000>;
		};
	};
};
```

`zephyr,code-partition = &slot0_partition` 表示应用永远按 slot0 地址链接。`slot1_partition` 只是下载区，不能直接运行。`scratch_partition` 是 MCUboot 交换 slot0 和 slot1 时使用的临时区域。

STM32H743 内部 Flash 扇区大小是 128KB，因此 scratch 至少要 128KB，并且要按扇区大小对齐。

## 5. Kconfig/prj.conf: software configuration

sysbuild 配置：

```conf
SB_CONFIG_BOOTLOADER_MCUBOOT=y
SB_CONFIG_MCUBOOT_MODE_SWAP_SCRATCH=y
SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y
```

应用侧配置：

```conf
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_STREAM_FLASH=y
CONFIG_IMG_MANAGER=y
CONFIG_MCUBOOT_IMG_MANAGER=y
CONFIG_REBOOT=y
CONFIG_NET_BUF=y
CONFIG_ZCBOR=y
CONFIG_MCUMGR=y
CONFIG_MCUMGR_GRP_IMG=y
CONFIG_MCUMGR_GRP_OS=y
CONFIG_MCUMGR_TRANSPORT_UDP=y
CONFIG_MCUMGR_TRANSPORT_UDP_IPV4=y
CONFIG_MCUMGR_TRANSPORT_UDP_PORT=1337
CONFIG_CRANER_ENABLE_OTA_CONTROL=y
CONFIG_CRANER_OTA_TEST_TIMEOUT_S=3600
```

`SB_CONFIG_MCUBOOT_MODE_SWAP_SCRATCH=y` 会让 MCUboot 子工程启用 `CONFIG_BOOT_SWAP_USING_SCRATCH=y`，同时让应用侧知道当前 bootloader 模式是 `CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_SCRATCH=y`。

## 6. Business/application code

OTA 控制代码位于：

```text
src/ota_control_app.c
```

核心 API：

```c
boot_request_upgrade(BOOT_UPGRADE_TEST);
boot_write_img_confirmed();
mcuboot_swap_type();
boot_is_img_confirmed();
```

升级闭环：

```text
1. PC 使用 mcumgr image upload 把 app_update_signed.bin 写入 slot1_partition
2. PC 使用 mcumgr image test <slot1_hash>，或串口 shell 执行 ota test
3. PC 使用 mcumgr reset，或串口 shell 执行 ota reboot
4. MCUboot 使用 scratch 交换 slot0 和 slot1
5. 新固件从 slot0 运行
6. 用户确认后执行 mcumgr image confirm 或 ota confirm
7. 若未确认，超时重启后 MCUboot 自动回滚
```

## 7. How to extend it

当前以太网 OTA 使用 Zephyr MCUmgr SMP over UDP：

```text
PC mcumgr image upload -> MCU slot1_partition
PC mcumgr image test   -> MCUboot trailer 标记 test
PC mcumgr reset        -> 重启进入新固件
PC mcumgr image confirm 或串口 shell ota confirm
```

后续如果要增加网页上传，则网页服务只负责替代 PC `mcumgr image upload/test/reset` 这几个动作，底层仍建议使用同一套 MCUboot swap 状态机。业务代码需要做三件事：

```text
1. 接收 app_update_signed.bin
2. 写入 slot1_partition
3. 调用 boot_request_upgrade(BOOT_UPGRADE_TEST)
```

## 8. Troubleshooting

### PowerShell 提示无法识别 mcumgr

现象：

```text
mcumgr : 无法将“mcumgr”项识别为 cmdlet、函数、脚本文件或可运行程序的名称
```

原因是 PC 没有安装 `mcumgr`，或者 `mcumgr.exe` 所在目录没有加入 `PATH`。按前置条件中的步骤安装 Go 和 `mcumgr`，然后确认：

```powershell
mcumgr version
```

如果 `mcumgr.exe` 已安装但仍找不到，临时加入 PATH：

```powershell
$env:Path += ";$env:USERPROFILE\go\bin"
```

永久生效需要把下面目录加入 Windows 用户环境变量 `Path`：

```text
C:\Users\<用户名>\go\bin
```

### mcumgr image list 无响应

按顺序检查：

```text
1. MCU 日志里以太网是否 link up
2. MCU IP 是否为 192.168.18.32
3. PC 是否能 ping 192.168.18.32
4. PC 和 MCU 是否在同一网段
5. UDP 1337 是否被防火墙拦截
6. prj.conf 是否启用 CONFIG_MCUMGR_TRANSPORT_UDP=y
```

### image test 不知道填哪个 hash

上传完成后执行：

```powershell
mcumgr --conntype udp --connstring 192.168.18.32:1337 image list
```

选择 `slot=1` 的 `hash`，用于：

```powershell
mcumgr --conntype udp --connstring 192.168.18.32:1337 image test <slot1_hash>
```

### MCUboot 报找不到 scratch

检查 DTS 是否存在：

```text
scratch_partition
label = "image-scratch"
```

同时检查 sysbuild 是否启用了：

```text
SB_CONFIG_MCUBOOT_MODE_SWAP_SCRATCH=y
```

### 新固件启动后又回到旧固件

这是正常回滚保护。新固件没有调用：

```text
ota confirm
```

或者没有执行：

```c
boot_write_img_confirmed();
```

### 上传后的固件不启动

检查上传的是：

```text
app_update_signed.bin
```

不要上传 `zephyr.bin`，也不要上传 unsigned image。

### 初次烧录后 1 小时自动重启

说明初始应用不是 confirmed 镜像。首次 SWD 烧录应使用：

```text
app_initial_confirmed.hex
```

### 固件太大

slot 大小是 768KB，signed image 必须小于 slot 可用空间。检查：

```text
build\<board>\craner_encoder_hub\zephyr\zephyr.signed.bin
```

如果固件继续增大，需要重新规划分区，或者把部分资源放到外部 W25Q64。
