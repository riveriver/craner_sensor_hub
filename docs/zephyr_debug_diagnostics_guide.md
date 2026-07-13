# Zephyr 调试诊断功能入门：栈溢出、栈回溯和 Coredump

## 这份文档解决什么问题

嵌入式程序运行一段时间后复位、卡死、HardFault、线程不再执行，最难的是判断问题到底发生在哪里。Zephyr 已经提供了一组调试诊断能力，可以帮助我们回答这些问题：

1. 某个线程的栈是不是不够用？
2. 是否发生了栈溢出或栈破坏？
3. 异常发生时，代码大概是从哪条调用链走到 fault 的？
4. 程序崩溃后，能不能把寄存器、线程、内存信息保存下来，之后再分析？

这些能力大致分成四类：

| 功能 | 主要用途 | 适合阶段 |
| --- | --- | --- |
| 栈溢出保护 | 尽早发现线程栈越界 | 开发、测试、量产 |
| 栈使用量统计 | 观察每个线程栈用了多少 | 开发、压力测试 |
| 栈回溯 | 异常时打印调用栈地址 | 开发、现场排障 |
| Coredump | 崩溃时导出现场数据 | 开发、现场排障、复杂问题复盘 |

## 栈溢出为什么危险

Zephyr 中每个线程都有自己的栈，例如：

```c
K_THREAD_DEFINE(system_health_tid, 1536,
		system_health_thread, NULL, NULL, NULL,
		5, 0, 0);
```

这里的 `1536` 就是这个线程的栈大小，单位是字节。

如果线程栈不够用，常见后果是：

1. 覆盖旁边的内存。
2. 改坏其他变量、队列、信号量、线程控制块。
3. 程序不一定马上崩溃，而是过一段时间随机异常。

所以栈溢出保护的目标不是让程序“带病运行”，而是尽早让问题暴露出来。

## 手段 1：硬件栈保护 CONFIG_HW_STACK_PROTECTION

推荐优先了解这个配置：

```conf
CONFIG_HW_STACK_PROTECTION=y
```

它的作用是利用 CPU/MPU 等硬件能力，在栈边界设置保护区。当线程栈越界访问保护区时，CPU 会触发 fault。

对于 STM32H743 这类 Cortex-M7 MCU，MPU 可以参与这类保护。

优点：

1. 发现问题比较及时。
2. 依赖硬件保护，可信度高。
3. 适合开发版和量产版都打开。

代价：

1. 会占用部分 MPU 资源。
2. 触发后通常是 fatal error，系统会进入异常处理或复位。

建议：

```conf
CONFIG_HW_STACK_PROTECTION=y
```

## 手段 2：栈哨兵 CONFIG_STACK_SENTINEL

配置：

```conf
CONFIG_STACK_SENTINEL=y
```

它会在线程栈边界放一个特殊标记。Zephyr 在合适的时机检查这个标记，如果标记被破坏，说明栈可能已经溢出。

优点：

1. 比较容易启用。
2. 不一定依赖 MPU。
3. 可以作为硬件栈保护之外的补充。

限制：

1. 它不是每条指令都检查，通常在线程切换等时机发现。
2. 如果栈溢出后还没来得及检查，系统可能已经被破坏。

建议开发调试时打开：

```conf
CONFIG_STACK_SENTINEL=y
```

## 手段 3：编译器栈保护 CONFIG_STACK_CANARIES

配置：

```conf
CONFIG_STACK_CANARIES=y
```

它是编译器层面的保护。编译器会在函数栈帧里放 canary 值，函数返回时检查 canary 是否被破坏。

它主要用来发现函数内部局部变量数组越界等问题。例如：

```c
void demo(void)
{
	char buf[16];

	/* 如果这里写爆 buf，就可能破坏 canary */
}
```

优点：

1. 能发现很多函数内部栈破坏问题。
2. 对防止隐蔽内存破坏有帮助。

代价：

1. 会增加少量代码体积。
2. 会有少量运行时开销。

建议：

```conf
CONFIG_STACK_CANARIES=y
```

## 手段 4：线程栈信息 CONFIG_THREAD_STACK_INFO

配置：

```conf
CONFIG_THREAD_STACK_INFO=y
CONFIG_INIT_STACKS=y
```

这两个配置通常配合使用。

`CONFIG_THREAD_STACK_INFO` 让 Zephyr 保存线程栈边界等信息。

`CONFIG_INIT_STACKS` 会在栈初始化时填充固定模式。这样后续可以通过“哪些区域还保持初始值”来估算栈用量。

这类功能主要用于回答：

```text
system_health_thread 栈给 1536 字节够不够？
modbus_rtu_client_thread 栈是不是太小？
shell 线程栈是不是压力太大？
```

建议开发调试时打开：

```conf
CONFIG_THREAD_STACK_INFO=y
CONFIG_INIT_STACKS=y
```

## 手段 5：Thread Analyzer 线程栈分析

配置：

```conf
CONFIG_THREAD_ANALYZER=y
CONFIG_THREAD_ANALYZER_USE_LOG=y
```

如果希望系统周期性打印线程栈使用情况，可以再打开：

```conf
CONFIG_THREAD_ANALYZER_AUTO=y
CONFIG_THREAD_ANALYZER_AUTO_INTERVAL=10
```

含义是每 10 秒自动输出一次线程栈统计。

典型输出会包含：

```text
thread name / stack size / used / unused / usage percentage
```

这个功能很适合压力测试。例如让 Modbus RTU、Modbus TCP、以太网、Shell、OTA 同时运行一段时间，再看每个线程的栈余量。

使用建议：

1. 开发阶段可以打开自动打印。
2. 量产阶段不建议长期自动打印，日志会变多。
3. 当某个线程栈使用率超过 80% 时，应该考虑增大栈。
4. 当某个线程栈长期只用很少，可以考虑减小栈，但不要抠得太紧。

开发调试推荐：

```conf
CONFIG_THREAD_ANALYZER=y
CONFIG_THREAD_ANALYZER_USE_LOG=y
CONFIG_THREAD_ANALYZER_AUTO=y
CONFIG_THREAD_ANALYZER_AUTO_INTERVAL=10
```

## 手段 6：异常栈回溯 CONFIG_EXCEPTION_STACK_TRACE

配置：

```conf
CONFIG_EXCEPTION_STACK_TRACE=y
```

当系统发生 fault、assert、panic 等 fatal error 时，如果当前 CPU 架构支持栈遍历，Zephyr 会尝试打印调用栈地址。

它帮助我们回答：

```text
程序是从哪个函数调用到异常位置的？
```

注意它打印出来的通常是地址，不一定直接是函数名。例如：

```text
call trace:
  0x08041234
  0x0803abcd
  0x0802f010
```

这些地址需要配合 `zephyr.elf` 转成源码位置。

可以用类似命令解析：

```powershell
arm-zephyr-eabi-addr2line -e build\craner_general_stm32h743vit6\zephyr\zephyr.elf -f -C 0x08041234
```

说明：

1. `-e` 后面是 ELF 文件。
2. `-f` 打印函数名。
3. `-C` 反解 C++ 符号；C 工程加上也没坏处。

限制：

1. 不是所有架构都支持。
2. 优化等级、frame pointer、栈破坏程度都会影响回溯质量。
3. 如果栈已经严重损坏，回溯可能不完整。

建议开发调试时打开：

```conf
CONFIG_EXCEPTION_STACK_TRACE=y
```

## 手段 7：Shell 查看线程信息

如果启用了 Shell 和线程监控，可以通过 shell 查看线程状态。

常见相关配置：

```conf
CONFIG_SHELL=y
CONFIG_THREAD_MONITOR=y
CONFIG_THREAD_NAME=y
CONFIG_THREAD_STACK_INFO=y
CONFIG_INIT_STACKS=y
```

Zephyr 的 kernel shell 模块可以提供线程相关命令，例如查看线程列表、栈使用情况等。实际可用命令取决于当前 `.config`。

上板后可以在 shell 里先输入：

```text
help
```

或者查看 thread 子命令：

```text
kernel threads
```

如果工程启用了对应 shell 模块，就可以直接在串口终端里观察线程状态。

这个方法适合现场临时查看，不需要等待系统崩溃。

## 手段 8：Coredump 崩溃转储

配置入口：

```conf
CONFIG_DEBUG_COREDUMP=y
```

Coredump 的作用是：系统发生 fatal error 时，把崩溃现场导出来。

导出的信息可以包括：

1. CPU 寄存器。
2. 当前线程信息。
3. 线程栈。
4. 指定内存区域。

它适合分析这类问题：

```text
设备在客户现场偶发 HardFault，但串口日志不完整。
```

### Coredump backend：日志输出

最简单的方式是通过日志输出：

```conf
CONFIG_DEBUG_COREDUMP=y
CONFIG_DEBUG_COREDUMP_BACKEND_LOGGING=y
```

优点：

1. 不需要额外 flash 分区。
2. 容易先跑通。

缺点：

1. 输出内容可能很长。
2. 如果系统复位太快、串口丢数据，信息可能不完整。
3. 不适合大量现场设备长期使用。

### Coredump backend：保存到 Flash 分区

也可以保存到 flash：

```conf
CONFIG_DEBUG_COREDUMP=y
CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION=y
CONFIG_DEBUG_COREDUMP_SHELL=y
```

优点：

1. 崩溃后数据留在设备里。
2. 重启后可以通过 shell 或后续通信读取。
3. 更适合现场问题追踪。

缺点：

1. 需要规划 flash 分区。
2. 写 flash 有耗时。
3. 要考虑分区大小、擦写寿命和 OTA 分区关系。

对当前项目来说，因为 OTA 分区和 app storage 还在梳理，第一版建议先用 logging backend，不急着写 flash。

## 推荐配置组合

## 开发调试版

适合实验室调试、压力测试、定位 HardFault：

```conf
CONFIG_ASSERT=y

CONFIG_THREAD_NAME=y
CONFIG_THREAD_MONITOR=y
CONFIG_THREAD_STACK_INFO=y
CONFIG_INIT_STACKS=y

CONFIG_HW_STACK_PROTECTION=y
CONFIG_STACK_SENTINEL=y
CONFIG_STACK_CANARIES=y

CONFIG_THREAD_ANALYZER=y
CONFIG_THREAD_ANALYZER_USE_LOG=y
CONFIG_THREAD_ANALYZER_AUTO=y
CONFIG_THREAD_ANALYZER_AUTO_INTERVAL=10

CONFIG_EXCEPTION_STACK_TRACE=y

CONFIG_DEBUG_COREDUMP=y
CONFIG_DEBUG_COREDUMP_BACKEND_LOGGING=y
```

特点：

1. 问题更容易暴露。
2. 日志更丰富。
3. 会增加一些代码体积和运行时开销。

## 量产基础保护版

适合保留基础安全保护，但不输出大量日志：

```conf
CONFIG_HW_STACK_PROTECTION=y
CONFIG_STACK_CANARIES=y
```

可以根据产品稳定性决定是否保留：

```conf
CONFIG_ASSERT=y
CONFIG_STACK_SENTINEL=y
```

量产版是否打开 `ASSERT` 要谨慎。如果 assert 表示“继续运行风险更大”，那打开是合理的；如果某些 assert 只是开发期检查，量产版可能需要关闭。

## 当前项目建议

对于 `craner_encoder_hub`，建议分两步做。

第一步：新增开发诊断配置文件，例如：

```text
debug_diagnostics.conf
```

内容使用开发调试版配置。需要调试时，通过额外 overlay conf 构建。

第二步：量产 `prj.conf` 只保留基础保护：

```conf
CONFIG_HW_STACK_PROTECTION=y
CONFIG_STACK_CANARIES=y
```

这样不会让默认固件日志过多，也不会轻易影响实时性。

## 如何判断线程栈大小是否合适

建议流程：

1. 打开 `CONFIG_INIT_STACKS`、`CONFIG_THREAD_STACK_INFO`、`CONFIG_THREAD_ANALYZER`。
2. 让设备进入最复杂工作状态。
3. 同时运行以太网、Modbus TCP、Modbus RTU、Shell、OTA 或其他业务。
4. 运行足够长时间。
5. 查看每个线程的最大栈使用量。

经验规则：

| 栈使用率 | 判断 |
| ---: | --- |
| 小于 50% | 通常比较宽裕 |
| 50% ~ 80% | 较合理 |
| 80% ~ 90% | 需要谨慎，建议增大 |
| 大于 90% | 风险较高，应增大 |

不要只看一次空闲状态下的结果。网络通信、日志打印、shell 命令、错误处理路径，都可能让栈使用量突然增加。

## 崩溃日志应该怎么读

当出现 fault 时，重点看这些信息：

1. fault 类型：HardFault、MemManage Fault、BusFault、UsageFault。
2. fault address：访问了哪个非法地址。
3. current thread：哪个线程触发。
4. call trace：调用栈地址。
5. 寄存器：PC、LR、SP。

常见判断：

| 现象 | 可能原因 |
| --- | --- |
| PC 指向非法地址 | 函数指针被破坏、栈破坏、跳转地址错误 |
| fault address 接近 0x00000000 | 空指针访问 |
| fault address 是奇怪随机值 | 野指针、内存破坏 |
| 当前线程栈使用率极高 | 栈不足或递归过深 |
| call trace 不完整 | 栈已经被破坏或优化影响回溯 |

## 和看门狗的关系

看门狗解决的是“系统卡死后能自动恢复”。

栈保护、回溯、Coredump 解决的是“为什么会卡死或崩溃”。

两者应该配合使用：

1. 看门狗负责兜底复位。
2. 栈保护负责尽早发现内存越界。
3. 回溯和 Coredump 负责留下证据。

如果只开看门狗，不开诊断功能，设备可能只是不断复位，但我们不知道根因。

## 排障建议

| 问题 | 优先打开 |
| --- | --- |
| 怀疑线程栈不够 | `THREAD_ANALYZER`、`INIT_STACKS`、`THREAD_STACK_INFO` |
| 怀疑栈溢出 | `HW_STACK_PROTECTION`、`STACK_SENTINEL` |
| 怀疑局部数组越界 | `STACK_CANARIES` |
| 发生 HardFault | `EXCEPTION_STACK_TRACE` |
| 现场偶发崩溃 | `DEBUG_COREDUMP` |
| 设备卡死无日志 | 看门狗 + Coredump 或持久化错误记录 |

## 推荐下一步

先不要一次性把所有诊断功能都放进默认 `prj.conf`。

建议新增一个调试专用配置文件，让开发人员需要时显式启用：

```powershell
west build -b craner_general_stm32h743vit6 . -d build\debug -- -DEXTRA_CONF_FILE=debug_diagnostics.conf
```

等确认各功能在当前板子上都能正常工作后，再决定哪些进入默认配置，哪些只用于调试构建。
