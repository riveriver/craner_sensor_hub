# 定时休眠功能设计

## 1. 设计目标

本设计用于 `craner_encoder_hub` 项目在 STM32H743 + Zephyr 4.4.1 上实现“按真实时间定时休眠、按 RTC 定时唤醒”的能力。设备后续会依赖 RTC 做定时休眠，所以时间不能只满足日志显示，而必须满足“可信、可追溯、可拒绝”的要求：当 RTC 不可信时，系统必须拒绝进入依赖绝对时间的休眠，不能冒险睡错或醒错。

本功能以 RTC LSE 时间保持设计为基础。RTC 使用 STM32H743 内部 RTC，时钟源使用 LSE 32.768 kHz，VBAT 纽扣电池保持 backup domain。定时休眠功能不直接相信 RTC calendar，而是先通过 backup register 中的可信标记判断 RTC 是否曾被 NTP、GPS 或人工设置过，确认时间可信后才允许设置 RTC alarm 并进入低功耗。

## 2. 功能特性概述

1. 系统新增统一的 `scheduled_sleep_service` 管理定时休眠请求，业务模块、Shell、MQTT 远程诊断都不直接操作低功耗和 RTC alarm；休眠前必须经过 `time_service` 和 `rtc_time_provider` 的可信时间检查，只有 RTC 时间合理、backup register 校验通过、时间来源可信时，才允许进入绝对时间休眠；休眠前服务会停止或挂起 MQTT、Modbus TCP、网络 DHCP、编码器采集、日志发送等可中断业务，关闭外设电源 GPIO，并设置 RTC alarm 作为唤醒源；唤醒后服务会恢复电源、网络、MQTT、Modbus 和采集线程，并记录本次休眠原因、计划唤醒时间、实际唤醒时间和误差；如果 RTC 不可信或业务拒绝休眠，系统会明确返回错误并保持在线运行；第一阶段优先实现安全可调试的 STOP/PM suspend 级休眠，第二阶段在硬件实测确认后再扩展到 Standby/系统复位式深度休眠。

## 3. 术语和边界

本文把休眠分为两类：

```text
相对休眠:
  例如 sleep 60 秒。它依赖系统 tick 或 RTC alarm，但不依赖真实年月日。

绝对时间休眠:
  例如睡到 2026-07-16T02:00:00Z。它依赖可信 RTC 时间，不能在 RTC 不可信时执行。
```

本项目后续要做的是“定时休眠”，默认理解为绝对时间休眠。也就是设备根据真实 UTC 时间设置下一次唤醒。该功能必须比普通 `k_sleep()` 更严格。

## 4. 低功耗模式选择

STM32H743 常见低功耗层级可以按风险从低到高理解：

```text
运行态 idle:
  CPU 空闲时由 Zephyr 自动 WFI，功耗下降有限，业务仍在线。

STOP:
  主时钟和多数外设停止，SRAM 可保持，唤醒后软件上下文基本还在。
  适合第一阶段实现和调试。

Standby:
  大部分电源域关闭，SRAM 通常不保持，唤醒接近一次 reset。
  功耗更低，但恢复流程复杂，需要依赖 backup domain 保存状态。

Shutdown:
  更深层低功耗，恢复更像冷启动。当前不建议第一阶段使用。
```

第一阶段推荐：

```text
默认模式: STOP / PM suspend-to-idle 或 Zephyr 支持的 STM32 低功耗挂起状态
目标: 先验证 RTC alarm 唤醒、业务暂停恢复、电源控制、安全拒绝策略
```

第二阶段再评估：

```text
Standby 模式
目标: 更低功耗
前提: RTC alarm 唤醒、backup register、启动原因识别、业务恢复流程全部实测通过
```

不建议一上来直接做 Standby。因为 Standby 唤醒后更像重启，网络、MQTT、业务线程、日志状态都要重建；如果没有完整的恢复状态机，调试会比较痛苦。

## 5. RTC 可信判断

定时休眠前必须先判断 RTC 是否可信。不能只看 `rtc_get_time()` 是否成功，也不能只看时间范围是否合理。

建议 RTC 可信判断包含四层：

```text
1. RTC 驱动可用，rtc_get_time() 成功
2. RTC 时间在合理范围内，例如 2026-01-01 到 2099-12-31
3. backup register 中存在项目 magic、version、valid 标记
4. backup register 记录通过 CRC 校验
```

只有全部通过，才能认为：

```text
rtc_trust = trusted
```

如果只满足前两项，只能认为：

```text
rtc_trust = plausible
```

`plausible` 表示“看起来像一个合理时间”，可以用于日志辅助显示，但不能用于定时休眠。

## 6. Backup Register 设计

STM32H743 的 RTC backup domain 中有 backup registers。Zephyr H7 dtsi 中已经有 `backup_regs` 节点，兼容 `st,stm32-bbram`，当前生成的 DTS 里它默认是 disabled。后续实现时应启用它，并打开 Zephyr BBRAM 驱动。

建议 DTS：

```dts
&rtc {
	clocks = <&rcc STM32_CLOCK(APB4, 16)>,
		 <&rcc STM32_SRC_LSE RTC_SEL(1)>;
	status = "okay";

	bbram: backup_regs {
		status = "okay";
	};
};
```

建议 Kconfig：

```conf
CONFIG_BBRAM=y
CONFIG_BBRAM_STM32=y
CONFIG_RTC_ALARM=y
```

backup register 中建议保存一个小结构：

```c
struct rtc_trust_record {
	uint32_t magic;              /* 'CRTR' */
	uint16_t version;            /* 1 */
	uint16_t flags;              /* valid, wake_scheduled, reserved */
	uint32_t boot_count;
	uint32_t trusted_source;     /* ntp, gps, manual */
	int64_t last_trusted_time_s;
	int64_t last_sync_uptime_ms;
	int64_t planned_wakeup_time_s;
	uint32_t planned_sleep_id;
	uint32_t crc32;
};
```

推荐 flags：

```text
bit0 valid:
  RTC 曾经由可信来源写入过。

bit1 wake_scheduled:
  当前存在计划中的 RTC alarm 休眠唤醒。

bit2 woke_from_alarm:
  启动阶段判断本次可能来自 RTC alarm 唤醒后设置，用于诊断。
```

CRC32 只覆盖 `crc32` 字段之前的内容。读取 backup record 时，如果 magic、version、CRC 任一不匹配，必须视为无效。

## 7. 时间可信状态

建议在 `time_service` 或 `rtc_time_provider` 中增加时间可信状态：

```text
invalid:
  RTC 不可用、未设置、时间范围不合理、backup 记录无效。

plausible:
  RTC 可读且时间范围合理，但没有可信标记或 CRC 不通过。

trusted:
  RTC 可读、时间合理、backup 记录有效，且最后一次可信来源为 NTP/GPS/manual。

synced:
  本次开机后已经由 NTP/GPS/manual 校准过。
```

定时休眠准入规则：

```text
trusted 或 synced:
  允许绝对时间休眠。

plausible:
  禁止绝对时间休眠，只允许用户明确请求短时相对休眠，并在命令输出中提示风险。

invalid:
  禁止所有依赖 RTC 唤醒的休眠。
```

## 8. 软件模块组织

建议新增三个模块：

```text
src/rtc_trust_store.c
src/rtc_trust_store.h
src/scheduled_sleep_service.c
src/scheduled_sleep_service.h
```

职责划分：

```text
rtc_time_provider:
  RTC calendar 读写、RTC alarm 设置、RTC 驱动状态。

rtc_trust_store:
  backup register 读写、magic/version/CRC 校验、可信标记维护、计划唤醒记录。

time_service:
  统一管理系统真实时间、时间来源、质量和校时模式。

scheduled_sleep_service:
  休眠请求仲裁、业务收尾、设置 alarm、进入低功耗、唤醒恢复。

power_control_app:
  提供外设电源开关 API，而不是只在 SYS_INIT 中打开电源。

network_service / mqtt_service_manager:
  提供 prepare_suspend/resume 或 stop/start 接口，配合休眠前后的网络状态机。
```

`main.c` 仍然保持薄，只做初始化：

```text
device_identity_service_init()
time_service_init()
network_service_init()
scheduled_sleep_service_init()
network_service_start()
```

## 9. 休眠请求模型

建议休眠请求结构：

```c
enum scheduled_sleep_mode {
	SCHEDULED_SLEEP_MODE_RELATIVE,
	SCHEDULED_SLEEP_MODE_UNTIL_UTC,
};

struct scheduled_sleep_request {
	enum scheduled_sleep_mode mode;
	int64_t wakeup_unix_time_s;
	uint32_t duration_s;
	uint32_t min_duration_s;
	uint32_t max_duration_s;
	bool allow_plausible_rtc;
	const char *reason;
};
```

对于正式业务，`allow_plausible_rtc` 默认必须是 false。Shell 调试可以开放短时相对休眠，但必须打印清楚当前时间是否可信。

## 10. 休眠准入检查

进入休眠前按顺序检查：

```text
1. 当前没有 OTA 写 flash、MCUboot confirm、settings 保存等关键操作。
2. 当前没有正在执行的 Modbus TCP 关键事务。
3. 编码器采集线程处于可暂停点。
4. MQTT 允许断开，或者已经发布 going_to_sleep 状态。
5. time_service 显示时间可信。
6. RTC provider 可用。
7. RTC alarm 可用。
8. wakeup_time 大于当前时间 + 最小安全间隔。
9. wakeup_time 不超过最大允许休眠时长。
10. backup register 写入计划唤醒记录成功。
```

建议默认限制：

```text
最小休眠时长: 10 秒
最大休眠时长: 24 小时
RTC alarm 设置提前量: 至少 2 秒
业务收尾超时: 5 秒
MQTT 离线发布超时: 2 秒
```

如果任一检查失败，休眠请求失败，设备继续运行。

## 11. 休眠前收尾流程

建议流程：

```text
scheduled_sleep_request()
  lock sleep state
  mark state = preparing
  publish MQTT status: going_to_sleep
  stop accepting MQTT shell state-changing commands
  stop or pause Modbus TCP server
  pause encoder polling threads at safe point
  flush logs
  stop DHCP/network or bring interface down
  power off Ethernet bridge / sensor power if allowed
  set RTC alarm
  write backup wake_scheduled record
  enter low power
```

注意：日志 backend 里 MQTT 和 UART 都可能阻塞或延迟。进入低功耗前不要无限等待日志 flush，必须有超时。

## 12. RTC Alarm 设计

Zephyr RTC API 支持 alarm：

```c
rtc_alarm_set_time(dev, id, mask, &alarm_time);
rtc_alarm_set_callback(dev, id, callback, user_data);
```

STM32 RTC alarm 支持字段包括：

```text
second
minute
hour
weekday
monthday
```

对本项目来说，建议第一版只设置完整的年月日时分秒转换后的 alarm 中可支持字段，并限制最大休眠时间不超过 24 小时。这样可以避免跨月、跨年 alarm 字段支持差异带来的复杂性。

第一版策略：

```text
只允许 duration_s <= 86400
把 wakeup_unix_time 转成 RTC calendar
使用 hour/minute/second/monthday 字段设置 alarm
唤醒后立即清除 wake_scheduled 标记
```

如果后续要支持多天、每周、每月周期唤醒，应在 `scheduled_sleep_service` 层做下一次唤醒时间计算，而不是依赖 RTC alarm 的重复匹配能力。

## 13. 进入低功耗方式

第一阶段建议不要直接调用芯片私有寄存器进入 Standby，而是优先走 Zephyr PM：

```conf
CONFIG_PM=y
CONFIG_PM_DEVICE=y
CONFIG_RTC_ALARM=y
```

然后在应用中通过 Zephyr PM 机制让系统进入可被 RTC alarm 唤醒的低功耗状态。具体 STM32H743 支持的 PM state 需要通过构建后的 `zephyr.dts` 和实际唤醒测试确认。

如果 Zephyr 对 STM32H743 的目标低功耗状态支持不足，再考虑在 `scheduled_sleep_service` 内做 STM32 专用实现，但要把芯片私有代码集中在一个文件里，不能散落到业务模块。

## 14. 唤醒恢复流程

STOP 类休眠唤醒后，软件上下文仍在，建议流程：

```text
RTC alarm interrupt wakes CPU
scheduled_sleep_service alarm callback sets wake flag
resume power rails
resume Ethernet bridge
restart network_service / DHCP
resume Modbus TCP server
resume encoder polling
resume MQTT manager
publish MQTT status: woke_up
clear backup wake_scheduled
record actual wakeup time and wake error
state = active
```

Standby 类休眠唤醒后，系统更像重新启动，建议流程：

```text
main() cold/warm boot
rtc_trust_store_init()
read backup wake_scheduled
read reset/wakeup reason
if wake_scheduled and RTC alarm flag:
  mark wake reason = scheduled_alarm
clear wake_scheduled
normal service init
publish MQTT status: woke_up
```

所以 Standby 阶段必须实现“启动原因识别”和“backup wake_scheduled 记录”，否则很难判断设备是按计划醒来，还是异常重启。

## 15. 外设电源控制

当前项目已有电源 GPIO：

```text
POWER_3V3_AND_CCTV
POWER_5V
POWER_NET_BRIGDE
```

现在 `power_control_app` 在 `SYS_INIT` 中默认全部打开。定时休眠需要把它改造成服务接口：

```text
power_control_service_enable_all()
power_control_service_disable_for_sleep()
power_control_service_restore_after_sleep()
power_control_service_get_status()
```

休眠时建议第一阶段只关闭确定安全的外设电源。比如 Ethernet bridge 可以关闭，但如果某一路电源影响 MCU、RTC、VBAT 或调试串口，就不能关闭。

## 16. 网络和业务处理

休眠前网络处理建议：

```text
MQTT:
  发布 going_to_sleep，等待短超时，然后主动 disconnect。

Modbus TCP:
  停止 accept 新连接，关闭现有 client socket。

Telnet Shell:
  休眠前会断开，唤醒后 DHCP ready 再恢复。

DHCP:
  唤醒后重新 DHCP，不假设原 IP 仍然有效。

Modbus RTU 编码器采集:
  在线程安全点暂停，唤醒后恢复。
```

如果业务要求“休眠前必须上报一次编码器快照”，应该由业务模块先完成快照上报，再提交休眠请求，而不是让休眠服务临时读所有业务数据。

## 17. Shell 命令设计

建议新增命令：

```sh
sleep_status
sleep_until 2026-07-16T02:00:00Z
sleep_for 300
sleep_cancel
sleep_test 30
rtc_trust_status
rtc_trust_clear
```

命令行为：

```text
sleep_status:
  显示当前休眠状态、最后一次请求、计划唤醒时间、实际唤醒时间、失败原因。

sleep_until:
  绝对时间休眠，必须要求 RTC trusted/synced。

sleep_for:
  相对时间休眠。正式模式下仍建议要求 RTC trusted，调试模式可允许短时间。

sleep_cancel:
  取消尚未进入低功耗的休眠请求。

sleep_test:
  开发调试命令，只允许 10~300 秒。

rtc_trust_status:
  显示 backup record、CRC、last trusted source、last trusted time。

rtc_trust_clear:
  清空可信标记，用于测试 VBAT/backup 失效流程。
```

## 18. MQTT 远程命令设计

MQTT 白名单第一阶段建议只开放查询：

```text
sleep_status
rtc_trust_status
```

不建议第一阶段开放远程 `sleep_until` 或 `sleep_for`，因为它会让设备离线，属于高风险动作。未来如果要开放，必须增加：

```text
命令签名
权限控制
最短/最长休眠限制
二次确认
可撤销窗口
审计日志
```

## 19. 状态机

建议 `scheduled_sleep_service` 状态：

```text
active:
  正常运行。

preparing:
  正在做业务收尾和 alarm 设置。

sleeping:
  已进入低功耗或即将进入低功耗。

waking:
  已唤醒，正在恢复电源、网络和业务。

failed:
  休眠请求失败，记录失败原因。
```

失败原因建议枚举：

```text
time_not_trusted
rtc_not_ready
rtc_alarm_not_supported
wakeup_time_too_soon
wakeup_time_too_far
business_busy
mqtt_offline_publish_timeout
network_stop_failed
power_control_failed
rtc_alarm_set_failed
pm_enter_failed
```

## 20. Kconfig 设计

建议新增配置：

```conf
CONFIG_CRANER_ENABLE_SCHEDULED_SLEEP_SERVICE=y
CONFIG_CRANER_SCHEDULED_SLEEP_MIN_DURATION_S=10
CONFIG_CRANER_SCHEDULED_SLEEP_MAX_DURATION_S=86400
CONFIG_CRANER_SCHEDULED_SLEEP_PREPARE_TIMEOUT_MS=5000
CONFIG_CRANER_SCHEDULED_SLEEP_MQTT_TIMEOUT_MS=2000
CONFIG_CRANER_SCHEDULED_SLEEP_REQUIRE_TRUSTED_RTC=y
CONFIG_CRANER_SCHEDULED_SLEEP_ALLOW_DEBUG_RELATIVE_SLEEP=n
```

依赖配置：

```conf
CONFIG_PM=y
CONFIG_PM_DEVICE=y
CONFIG_RTC=y
CONFIG_RTC_STM32=y
CONFIG_RTC_ALARM=y
CONFIG_BBRAM=y
CONFIG_BBRAM_STM32=y
```

第一阶段如果 PM 进入深度状态不稳定，可以先启用 RTC alarm 和服务状态机，但把真正低功耗动作替换为 `k_sleep()`，用于验证业务收尾和唤醒恢复流程。等 alarm 唤醒链路确认后，再切换到真正 PM。

## 21. 数据记录

建议 `scheduled_sleep_service` 保存最近一次记录：

```text
last_request_id
last_request_reason
last_state
last_fail_reason
planned_sleep_start_s
planned_wakeup_s
actual_sleep_start_s
actual_wakeup_s
wake_error_s
last_wake_reason
sleep_count
fail_count
```

这份状态应同时支持 Shell 查询和 MQTT 状态上报。进入 Standby 前，最关键的字段还要写入 backup register，因为普通 RAM 会丢。

## 22. 测试用例

1. RTC 未可信时拒绝休眠：清空 backup trust record，执行 `sleep_until`，应返回 `time_not_trusted`，设备保持在线。
2. NTP 校时后允许休眠：联网完成 NTP，同步写 RTC 和 backup trust record，执行 `sleep_test 30`，设备应进入休眠并约 30 秒后恢复。
3. 手动设置后允许休眠：无网络时执行 `time_set`，写入 trusted manual 标记，再执行短时休眠，应能按计划唤醒。
4. 拔掉 VBAT 后拒绝休眠：校时后断主电并拔掉纽扣电池，再上电，backup trust record 应失效，绝对时间休眠必须拒绝。
5. RTC 时间被改乱后拒绝休眠：手动写入超出范围的 RTC 时间，`rtc_trust_status` 应显示 invalid，休眠拒绝。
6. MQTT/网络恢复测试：休眠前 MQTT 发布 going_to_sleep，唤醒 DHCP ready 后发布 woke_up。
7. Modbus TCP 恢复测试：休眠前关闭连接，唤醒后 Modbus TCP 重新可连接。
8. 编码器采集恢复测试：休眠前暂停采集，唤醒后继续更新寄存器快照。
9. 最短时间保护测试：执行 `sleep_for 1`，应返回 `wakeup_time_too_soon`。
10. 最长时间保护测试：执行超过最大休眠时间的请求，应返回 `wakeup_time_too_far`。
11. RTC alarm 误差测试：连续执行 10 次 `sleep_test 30`，统计实际唤醒误差。
12. Standby 专项测试：第二阶段开启 Standby 后，验证唤醒原因、backup 记录和业务重建。

## 23. 实施步骤

第一步：时间可信基础。

```text
1. 启用 bbram backup_regs
2. 新增 rtc_trust_store
3. NTP/GPS/manual 成功后写 trusted record
4. rtc_status/time_status 增加 trust 状态
```

第二步：RTC alarm 基础。

```text
1. 启用 CONFIG_RTC_ALARM
2. rtc_time_provider 增加 alarm set/clear/status
3. Shell 增加 rtc_alarm_test
4. 验证 RTC alarm callback 可以触发
```

第三步：休眠状态机。

```text
1. 新增 scheduled_sleep_service
2. 实现准入检查和状态记录
3. 实现 sleep_status/sleep_test
4. 第一阶段可用 k_sleep 模拟低功耗
```

第四步：真实低功耗。

```text
1. 接入 Zephyr PM
2. 进入 STOP 类低功耗
3. RTC alarm 唤醒
4. 恢复网络和业务
```

第五步：Standby 深度休眠。

```text
1. 增加启动原因识别
2. 使用 backup record 恢复计划休眠上下文
3. 实测功耗和唤醒可靠性
4. 再开放正式配置
```

## 24. 设计结论

定时休眠不能直接建立在“RTC 能读出一个合理时间”之上，必须建立在“RTC 时间可信”之上。本项目应先实现 backup register 可信标记和 CRC，再实现 RTC alarm 和休眠状态机。第一阶段优先做 STOP/可恢复休眠，保证网络、MQTT、Modbus、编码器采集都能安全暂停和恢复；Standby 深度休眠等唤醒原因和 backup 记录实测稳定后再启用。这样做会慢一点，但不会让设备因为一个不可信的 RTC 时间睡错、醒错，稳是第一生产力。
