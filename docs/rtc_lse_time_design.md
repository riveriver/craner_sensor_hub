# STM32H743 RTC LSE 时间保持设计

## 1. 设计目标

本设计用于 `craner_encoder_hub` 项目的板卡 `craner_general_stm32h743vit6`。目标是在 STM32H743 内部 RTC 上实现可靠的真实时间保持：设备正常上电时由系统读取 RTC 作为初始真实时间；设备通过 Shell、NTP、GPS 等来源校正时间；设备断主电后，RTC 在纽扣电池 VBAT 供电下继续走时；再次上电后，业务仍能得到一个连续、可信度明确的时间。

RTC 晶振明确使用 **LSE 32.768 kHz 外部低速晶振**。LSE 是本项目 RTC 的正式产品方案，LSI 只作为极端硬件缺失时的低精度临时方案，不建议进入正式版本。

## 2. 功能特性概述

1. 使用 STM32H743 内部 RTC 和外部 LSE 32.768 kHz 晶振实现断电走时，RTC 由 VBAT 纽扣电池维持，主电源掉电后仍保留时间；系统上电后优先读取 RTC，如果 RTC 时间有效，则把它作为真实时间基准提供给业务；网络可用后由 NTP 自动校时，后续预留 GPS 高精度校时入口，同时支持 Shell 手动设置时间；时间服务统一管理 boot tick、RTC、NTP、GPS 等时间源，业务代码只访问 `time_service`，不直接操作 RTC、NTP 或 GPS；系统支持默认自动校正模式，也支持仅手动校正模式，便于现场网络受限或需要人工控制时间的场景；所有时间状态都可以通过本地 Shell、Telnet Shell 和 MQTT 白名单远程诊断命令查看。

## 3. 硬件方案

RTC 时钟源使用 LSE：

```text
晶振类型: 32.768 kHz watch crystal
连接引脚: OSC32_IN / OSC32_OUT
常见管脚: PC14 / PC15
供电保持: VBAT 纽扣电池
用途: RTC calendar 断电继续走时
```

硬件设计需要确认以下内容：

1. 板卡已经焊接 32.768 kHz LSE 晶振，且晶振连接到 STM32H743 的 `OSC32_IN` 和 `OSC32_OUT`。
2. 晶振负载电容符合所选晶体的规格，不能只按经验固定取值。负载电容不合适会导致 LSE 起振慢、无法起振或长期走时偏差偏大。
3. VBAT 已接纽扣电池或备用电源，并满足 STM32H743 backup domain 的供电要求。
4. 如果 PC14/PC15 在板卡上被复用为普通 GPIO，则不能同时作为 LSE 使用。RTC 产品方案里应固定保留给 LSE。
5. 如果需要量产精度评估，应预留 LSE 测试点或通过 MCO/内部校准方式验证 32.768 kHz 时钟。

不推荐的方案：

```text
LSI: 内部 RC，精度差，温漂大，只适合看门狗或低精度计时。
HSE/128: 主电源相关，不适合断主电后由 VBAT 保持 RTC。
软件 tick: 断电后停止，不能作为真实日历时间。
```

## 4. Zephyr Devicetree 设计

当前板级 DTS 已配置 HSE、HSI48、PLL、以太网和串口，但尚未启用 LSE/RTC。后续实现 RTC 时，应在：

```text
boards/craner/craner_general_stm32h743vit6/craner_general_stm32h743vit6.dts
```

增加 LSE 和 RTC 配置。STM32H7 在 Zephyr 4.4.1 中的推荐写法如下：

```dts
&clk_lse {
	status = "okay";
};

&rtc {
	clocks = <&rcc STM32_CLOCK(APB4, 16)>,
		 <&rcc STM32_SRC_LSE RTC_SEL(1)>;
	status = "okay";
};
```

含义说明：

1. `&clk_lse` 表示启用 32.768 kHz 外部低速晶振。STM32H7 SoC dtsi 中已经定义了默认频率 `32768`，板级 DTS 只需要把它打开。
2. `STM32_CLOCK(APB4, 16)` 是 RTC 外设总线时钟。
3. `STM32_SRC_LSE RTC_SEL(1)` 表示 RTC 的时钟源选择 LSE。这里的 `RTC_SEL(1)` 对应 STM32 backup domain 中 RTC clock source 选择为 LSE。
4. `status = "okay"` 表示 Zephyr 可以创建设备实例，后续 `CONFIG_RTC` 和 `CONFIG_RTC_STM32` 才有实际硬件可绑定。

如果未来要诊断 LSE 起振问题，可以临时把 `driving-capability` 写到 `&clk_lse` 中，但正式值应结合晶体规格和实测确定：

```dts
&clk_lse {
	driving-capability = <0>;
	status = "okay";
};
```

不要在没有硬件依据时随意提高 LSE drive strength。驱动能力过低可能不起振，过高可能增加功耗并影响晶体寿命或稳定性。

## 5. Kconfig 设计

RTC 基础配置建议放在 `prj.conf`：

```conf
CONFIG_RTC=y
CONFIG_RTC_STM32=y
```

如果需要使用 Zephyr 自带 RTC Shell 辅助调试，可以在开发阶段打开：

```conf
CONFIG_RTC_SHELL=y
```

正式业务仍建议使用项目自己的 Shell 命令，例如：

```text
time_status
time_sync
time_set
rtc_status
rtc_set
time_mode auto
time_mode manual
```

原因是项目自己的命令可以输出 `time_service` 的完整状态，包括当前时间源、时间可信度、RTC 是否有效、NTP 是否成功、GPS 是否接入、当前校时模式等，而不是只暴露 RTC 驱动本身。

## 6. 软件架构

时间相关代码继续采用分层设计：

```text
time_service
  统一管理当前系统真实时间、时间源优先级、可信度、校时模式

rtc_time_provider
  封装 STM32 RTC 读写、有效性判断、断电保持状态

ntp_time_provider
  现阶段由 time_service 内部或独立模块触发 SNTP 校时

gps_time_provider
  未来接入 GPS 后提供高精度时间

shell_app / mqtt_shell_service
  只调用 time_service 暴露的 API，不直接碰 RTC 驱动
```

建议新增文件：

```text
src/rtc_time_provider.c
src/rtc_time_provider.h
```

`rtc_time_provider` 只负责 RTC 硬件细节：

```text
rtc_time_provider_init()
rtc_time_provider_is_valid()
rtc_time_provider_get_unix_time()
rtc_time_provider_set_unix_time()
rtc_time_provider_get_status()
```

`time_service` 负责策略：

```text
上电:
  boot_tick 可用，但不是真实时间
  读取 RTC
  RTC 有效则设置 SYS_CLOCK_REALTIME
  通知业务当前 source=rtc, quality=estimated

DHCP ready:
  如果是 auto 模式，触发 NTP 校时
  NTP 成功后设置 SYS_CLOCK_REALTIME
  同步写回 RTC
  source=ntp, quality=synced

Shell 手动设置:
  设置 SYS_CLOCK_REALTIME
  写回 RTC
  source=manual 或 rtc/manual，quality=synced

GPS 接入后:
  GPS 时间有效时更新 SYS_CLOCK_REALTIME
  写回 RTC
  source=gps, quality=high_precision
```

## 7. 时间源优先级

建议优先级保持如下：

```text
GPS > NTP > Shell manual > RTC > boot_tick
```

解释：

1. `boot_tick` 只能表示本次开机后的单调运行时间，不能表示真实年月日。
2. `RTC` 可以跨断电保持，是上电初始真实时间的主要来源，但可能有长期漂移。
3. `Shell manual` 是人工确认后的时间，可信度高于 RTC 的自然走时。
4. `NTP` 是网络校时，适合默认自动模式。
5. `GPS` 未来作为最高精度来源，适合现场没有公网但有 GNSS 模块的场景。

## 8. 校时模式

系统需要支持两种模式：

```text
auto: 默认模式。网络 ready 后自动 NTP 校时，未来 GPS 有效后自动 GPS 校时。
manual: 仅手动校正。系统可以读取 RTC，但不会自动用 NTP/GPS 覆盖当前时间。
```

建议状态保存在 settings 或 app-storage 中，防止重启后丢失：

```text
time/correction_mode = auto | manual
```

默认值为 `auto`。如果 settings 尚未实现，可以先使用编译期默认值，后续再持久化。

自动模式下的处理：

1. 上电读取 RTC。
2. RTC 有效则先使用 RTC，避免网络校时前业务完全没有真实时间。
3. DHCP ready 后触发 NTP。
4. NTP 成功后更新系统时间和 RTC。
5. NTP 失败不清空 RTC 时间，只降低网络校时状态，并按指数退避重试。

手动模式下的处理：

1. 上电仍读取 RTC。
2. 不自动触发 NTP/GPS 覆盖时间。
3. 用户通过 Shell 或 MQTT 白名单远程命令手动设置时间。
4. 手动设置成功后写入 RTC。

## 9. RTC 有效性判断

STM32 RTC 自身保存的是日历时间，但仅仅读到一个时间不代表它可信。建议使用“时间范围 + backup 标记”的组合判断。

建议策略：

```text
RTC 时间 >= 固件定义的最小有效时间
RTC 时间 <= 固件定义的最大合理时间
backup register 中存在项目 magic
backup register 中存在 time_valid 标记
```

最小有效时间可设置为项目首个发布版本附近，例如：

```text
2026-01-01 00:00:00 UTC
```

最大合理时间可以设置为：

```text
2099-12-31 23:59:59 UTC
```

如果 Zephyr RTC 驱动未直接提供 backup register API，可以先只做时间范围判断，后续再增加 STM32 backup register 或 backup SRAM 支持。STM32H743 有 backup domain，项目后续也可以启用 `backup_sram` 存储少量断电保持状态。

## 10. 与现有 time_service 的关系

当前 `time_service` 已经具备 NTP 校时和未来 RTC/GPS provider 接口。RTC 实现后，不建议让业务模块直接调用 Zephyr `rtc_get_time()` 或 `rtc_set_time()`，而应该统一走：

```c
time_service_is_time_valid();
time_service_unix_time_get();
time_service_format_iso8601();
time_service_update_from_source();
```

`rtc_time_provider` 在启动时把 RTC 时间上报给 `time_service`：

```c
time_service_update_from_source(TIME_SERVICE_SOURCE_RTC,
                                TIME_SERVICE_QUALITY_ESTIMATED,
                                unix_time_s);
```

NTP 或 GPS 成功后，`time_service` 再调用 RTC provider 写回硬件 RTC。这样可以保证业务看到的是同一套时间状态，不会出现“RTC 是一个时间、系统 clock 是另一个时间、MQTT 上报又是第三个时间”的混乱。

## 11. Shell 与 MQTT 诊断命令

建议新增或扩展以下 Shell 命令：

```sh
time_status
time_sync
time_mode
time_set
rtc_status
rtc_get
rtc_set
```

输出示例：

```text
time_status
valid: yes
source: ntp
quality: synced
mode: auto
unix_time: 1784080800
iso8601_utc: 2026-07-15T02:00:00Z
rtc_valid: yes
rtc_lse: enabled
last_ntp_sync: 2026-07-15T02:00:01Z
```

MQTT 远程诊断命令只开放白名单。建议允许：

```text
time_status
time_sync
rtc_status
```

`time_set` 和 `rtc_set` 属于会改变设备状态的命令，默认不建议开放到 MQTT。若未来必须远程设置时间，应增加权限、签名或一次性令牌机制。

## 12. NTP/GPS 写回 RTC 策略

为了减少不必要的 RTC 写操作，建议只有在时间偏差超过阈值时才写 RTC：

```text
abs(new_time - rtc_time) >= 2 秒时写回 RTC
```

如果 NTP 每小时同步一次，而 RTC 只差 0 或 1 秒，可以不写。这样做的好处是减少 backup domain 写操作和日志噪声，也避免频繁校时导致诊断困难。

建议记录最近一次校时信息：

```text
last_sync_source
last_sync_unix_time
last_sync_offset_s
last_sync_result
last_sync_uptime_ms
```

## 13. 启动流程

推荐启动顺序：

```text
main()
  device_identity_service_init()
  rtc_time_provider_init()
  time_service_init()
  network_service_init()
  mqtt_service_manager_init()
  network_service_start()
```

其中 `rtc_time_provider_init()` 应尽量早执行，因为网络、MQTT、日志上报都可能需要时间戳。`time_service_init()` 可以在初始化时读取 RTC provider，也可以由 main 显式先初始化 RTC，再把结果交给 time_service。为了保持 `main.c` 简洁，建议把读取 RTC 的动作封装在 `time_service_init()` 内部，main 只调用服务初始化函数。

## 14. 测试用例

1. 冷启动无网络测试：拔掉网线，上电后执行 `time_status`，应显示 RTC 时间有效，source 为 `rtc`，quality 为 `estimated`。如果 RTC 从未设置，应显示 `valid: no` 或 source 为 `boot_tick`。
2. NTP 自动校时测试：插入可访问 NTP 的网络，上电后 DHCP ready，等待 NTP 成功，`time_status` 应显示 source 为 `ntp`，quality 为 `synced`，RTC 已被写回。
3. 断电保持测试：NTP 校时成功后断主电，保持 VBAT 纽扣电池，等待 5 分钟后重新上电，不联网，`time_status` 应显示 RTC 时间继续前进，误差在 LSE 晶体允许范围内。
4. 拔掉 VBAT 测试：断主电并移除纽扣电池，再上电，RTC 应被判断为无效，系统不能把随机 RTC 值当作可信真实时间。
5. 手动模式测试：设置 `time_mode manual` 后重启，网络 ready 后不应自动用 NTP 覆盖当前时间；手动执行 `time_sync` 时可以按设计允许或拒绝，需要在命令说明中明确。
6. Shell 手动设置测试：执行 `time_set 2026-07-15T10:00:00Z`，系统时间和 RTC 都应更新，重启后读取到相同时间附近的 RTC 值。
7. MQTT 诊断测试：通过 MQTT 白名单命令执行 `time_status` 和 `rtc_status`，能看到 RTC/LSE/校时状态；不能执行未授权的 `rtc_set`。
8. LSE 硬件异常测试：如果移除或损坏 LSE 晶振，RTC 初始化应失败或状态异常，日志应明确提示 LSE/RTC 不可用，系统仍可依赖 boot tick 和 NTP 临时运行。

## 15. 故障排查

如果 RTC 驱动没有出现，先检查生成文件：

```powershell
Select-String -Path build\craner_general_stm32h743vit6\zephyr\.config -Pattern "CONFIG_RTC"
Select-String -Path build\craner_general_stm32h743vit6\zephyr\zephyr.dts -Pattern "rtc@58004000|clk-lse|STM32_SRC_LSE"
```

如果 `.config` 里没有 `CONFIG_RTC_STM32=y`，通常是 `&rtc` 没有 `status = "okay"`，或者 `CONFIG_RTC` 没有打开。

如果 RTC 初始化失败，优先检查：

1. DTS 是否启用了 `&clk_lse`。
2. `&rtc` 是否选择了 `STM32_SRC_LSE RTC_SEL(1)`。
3. 硬件是否真的焊接 LSE 晶振。
4. VBAT 是否供电。
5. PC14/PC15 是否被其他电路占用。
6. LSE 负载电容是否与晶体规格匹配。

如果 RTC 时间重启后丢失，优先检查 VBAT 和 backup domain。RTC 时间能在软件复位后保持，不代表能在断主电后保持；断主电保持必须依赖 VBAT。

## 16. 实施步骤

第一步只启用 RTC 硬件：

```text
1. DTS 启用 &clk_lse
2. DTS 启用 &rtc 并选择 STM32_SRC_LSE
3. prj.conf 启用 CONFIG_RTC
4. 编译并确认 .config / zephyr.dts
```

第二步增加 RTC provider：

```text
1. 新增 src/rtc_time_provider.c/.h
2. 实现 RTC get/set/status
3. time_service 上电读取 RTC
4. NTP 成功后写回 RTC
```

第三步完善校时模式和持久化：

```text
1. 新增 auto/manual 模式
2. Shell 命令 time_mode
3. 可选 settings 持久化
4. MQTT 诊断只开放安全白名单
```

第四步接入 GPS：

```text
1. 增加 gps_time_provider
2. GPS 有效时更新 time_service
3. GPS 成功后按阈值写回 RTC
```

## 17. 设计结论

本项目 RTC 正式方案使用 STM32H743 内部 RTC + 外部 LSE 32.768 kHz 晶振 + VBAT 纽扣电池。软件上不让业务直接访问 RTC，而是通过 `time_service` 统一管理 RTC、NTP、GPS、Shell 手动校时和 boot tick。默认自动校正，现场需要时可切换到仅手动校正。这样既能满足断电走时，也能为 MQTT、日志、编码器数据上报和远程诊断提供一致的时间基础。
