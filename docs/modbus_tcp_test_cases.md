# Modbus TCP 测试用例

本文档面向测试组，用于对已经烧录好固件、已经封盒的整机进行 Modbus TCP 功能验收。测试组不需要构建固件、烧录固件、打开外壳、连接调试器或使用串口，只通过以太网完成测试，并输出每条用例的测试结果和缺陷记录。

本文档覆盖 Modbus TCP 连接、OpenModScan 读写、地址空间边界、权限控制、多客户端连接、长时间压力测试，以及 Modbus 寄存器持久化存储。通用持久化存储服务、设备参数和 CoreDump 测试见 [persistent_storage_service_test_cases.md](persistent_storage_service_test_cases.md)。

## 1. 测试范围

1. Modbus TCP 基础通信：验证设备 502 端口可连接，Unit ID 为 `1`，OpenModScan 可正常读写。

2. 地址空间：验证当前线圈地址空间为 `0..9`，保持寄存器地址空间为 `0..9`，输入寄存器地址空间为 `0..99`。

3. 未定义地址规则：地址空间内未定义地址读取返回 `0`；写入未定义地址失败；地址空间外访问失败。

4. 权限控制：验证 Coil 地址 `0` 为 write-only；Input Register 为 read-only；Holding Register 地址 `0` 为 read/write。

5. Modbus 寄存器持久化：验证 Holding Register 地址 `0` 写入后保存到 `modbus-store`，重启后恢复。

6. 延迟保存和手动保存：验证写持久化寄存器后 `dirty` 状态、延迟保存、`modbus_store_save` 强制保存。

7. 双 bank 和断电恢复：从黑盒行为验证保存过程中断电不会恢复随机值或损坏值。

8. 多客户端：验证固定客户端池下多个 OpenModScan 连接同时读写时系统稳定。

## 2. 测试准备

1. 被测设备准备：

   测试对象为已经烧录好正式测试固件、已经封盒的整机。测试组不打开外壳，不连接 SWD/J-Link/ST-Link，不使用串口，不重新烧录固件。开发人员在测试前必须提供设备 mDNS 主机名、本轮测试是否允许清除 `modbus-store`、最大 Modbus TCP 客户端数配置，以及本版本正式 Modbus 寄存器表。

   开发人员提供的 Modbus 寄存器表至少应包含以下信息：

   ```text
   寄存器类型: Coil / Discrete Input / Input Register / Holding Register
   功能码: FC01 / FC02 / FC03 / FC04 / FC05 / FC06 / FC15 / FC16
   协议地址: 0-based address
   显示地址: 如 00001 / 10001 / 30001 / 40001，如适用
   名称: 如 REG_HOLDING_RESERVER
   默认值:
   数据类型: bool / uint16 / int16 / uint32 / int32 / float / bitfield 等
   单位和比例系数:
   权限: read-only / write-only / read-write
   是否持久化: yes / no
   合法范围:
   写入副作用: 如复位、清零、触发保存等
   备注:
   ```

   如果开发人员提供的正式寄存器表与本文档“当前 Modbus 寄存器定义”不一致，以开发人员提供的正式寄存器表为准，并在测试报告中记录差异。

2. 网络准备：

   在 PC 上使用 mDNS 主机名发现设备：

   ```powershell
   ping craner-{project}-{type}-{name_uid}.local
   ```

   测试报告中必须记录实际使用的主机名和解析到的 IP 地址。

3. Telnet Shell 准备：

   整机产测 Shell 统一通过以太网 Telnet 访问，不使用串口。测试工具使用 MobaXterm，新建 Telnet Session：

   ```text
   Remote host: craner-{project}-{type}-{name_uid}.local
   Port: 23
   Terminal: Telnet
   ```

   Modbus 持久化测试需要通过 Telnet Shell 执行：

   ```text
   modbus_store_status
   modbus_store_save
   modbus_store_clear
   storage_status
   ```

4. OpenModScan 准备：

   本项目 Modbus TCP 测试统一使用 OpenModScan。连接参数如下：

   ```text
   Connection: Modbus TCP/IP
   Server/IP: 设备 IP 或 craner-{project}-{type}-{name_uid}.local
   Port: 502
   Slave ID / Unit ID: 1
   Address base: 0-based
   Timeout: 1000 ms
   ```

   如果 OpenModScan 当前界面使用 1-based 地址显示，需要测试人员将本文档地址加 1 填入。例如本文档 Holding Register 地址 `0`，在 1-based 显示模式下可能需要填 `1`。测试记录中必须注明 OpenModScan 使用的是 0-based 还是 1-based。

5. OpenModScan 寄存器类型对应关系：

   ```text
   Coil               -> FC01 Read Coils / FC05 Write Single Coil
   Input Register     -> FC04 Read Input Registers
   Holding Register   -> FC03 Read Holding Registers / FC06 Write Single Register
   ```

   本文档统一使用协议地址，例如 Holding Register 地址 `0`。如果 OpenModScan 使用传统显示地址，Holding Register 地址 `0` 可能显示为 `40001`，Input Register 地址 `0` 可能显示为 `30001`，Coil 地址 `0` 可能显示为 `00001`。实际测试以工具发送的协议地址为准。

6. 初始清理建议：

   如果本轮测试允许清除历史 Modbus 持久化数据，建议测试开始前通过 MobaXterm Telnet Shell 执行：

   ```text
   modbus_store_clear
   reboot
   ```

   `modbus_store_clear` 是破坏性维护命令。执行前必须确认本轮测试允许清除历史 Modbus 持久化数据。如果 Telnet Shell 执行 `reboot` 后连接断开，等待设备重启完成后在 MobaXterm 中重新连接 Telnet。

## 3. 当前 Modbus 寄存器定义

本章节列出当前代码中已知的最小寄存器定义，用于测试组理解基础行为。正式验收时必须以开发人员提供的本版本 Modbus 寄存器表为准。

1. Coil：

   ```text
   地址空间: 0..9
   已定义地址: 0
   地址 0: REG_COIL_RESERVER
   权限: write-only
   持久化: no
   ```

2. Input Register：

   ```text
   地址空间: 0..99
   已定义地址: 0..17
   权限: read-only
   持久化: no
   ```

3. Holding Register：

   ```text
   地址空间: 0..9
   已定义地址: 0
   地址 0: REG_HOLDING_RESERVER
   权限: read/write
   持久化: yes
   ```

4. 未定义地址规则：

   地址在地址空间内但没有定义时，读取返回 `0`；写入未定义地址失败。地址超过地址空间时，读写都失败。

## 4. 测试结果记录要求

1. 每条测试用例必须记录测试结果，结果只能填写 `通过`、`失败`、`阻塞` 或 `不适用`。

2. `通过` 表示实际结果与预期结果一致。

3. `失败` 表示连接结果、Modbus 返回值、保存结果、重启恢复结果或维护命令输出与预期不一致。失败时必须记录缺陷编号。

4. `阻塞` 表示因为网络环境、MobaXterm、OpenModScan、供电条件等外部原因导致用例无法执行。阻塞时必须记录阻塞原因。

5. 每条失败用例至少记录以下证据：设备主机名、设备 IP、测试时间、OpenModScan 截图或返回值、MobaXterm Telnet 输出、相关 Shell 命令输出、重启或断电操作时间点。

## 5. 测试验收总表

测试组先根据下表进行总体验收记录，再按后续 MBTCP-xx 用例执行详细测试。`验收结果` 一栏由测试组填写，只能填写 `通过`、`失败`、`阻塞` 或 `不适用`；失败时填写缺陷编号。

| 编号 | 测试内容 | 验收标准 | 验收结果 |
| --- | --- | --- | --- |
| MBTCP-00 | 开发寄存器表输入检查 | 开发人员已提供本版本正式 Modbus 寄存器表，包含类型、功能码、0-based 地址、名称、默认值、数据类型、权限、是否持久化、合法范围和写入副作用。 |  |
| MBTCP-01 | Modbus TCP 连接检查 | OpenModScan 可连接 `craner-{project}-{type}-{name_uid}.local:502`，Unit ID `1` 可通信。 |  |
| MBTCP-02 | Input Register 基础读取 | FC04 读取 Input Register 地址 `0..17` 成功。 |  |
| MBTCP-03 | Holding Register 基础读写 | FC06 写 Holding Register 地址 `0` 成功，FC03 读取返回写入值。 |  |
| MBTCP-04 | Coil write-only 行为 | Coil 地址 `0` 读取失败、写入成功，且写入不触发持久化保存。 |  |
| MBTCP-05 | Coil 未定义地址读取 | Coil 地址 `1` 在地址空间内未定义，读取成功并返回 OFF/false。 |  |
| MBTCP-06 | Coil 越界访问 | Coil 地址 `10` 超出 `0..9`，读写均失败。 |  |
| MBTCP-07 | Holding 未定义地址读取 | Holding Register 地址 `1` 在地址空间内未定义，读取成功并返回 `0`。 |  |
| MBTCP-08 | Holding 未定义地址写入失败 | 写 Holding Register 地址 `1` 失败，且不触发 `dirty=yes`。 |  |
| MBTCP-09 | Holding 越界访问 | Holding Register 地址 `10` 超出 `0..9`，读写均失败。 |  |
| MBTCP-10 | Input 未定义地址读取 | Input Register 地址 `18` 在地址空间内未定义，读取成功并返回 `0`。 |  |
| MBTCP-11 | Input 越界读取 | Input Register 地址 `100` 超出 `0..99`，读取失败。 |  |
| MBTCP-12 | Modbus store 初始状态 | 清除并重启后 `modbus_store_status` 显示 `initialized=yes`、`dirty=no`、`active_bank_valid=no`、`last_error=0`。 |  |
| MBTCP-13 | Holding Register 延迟持久化保存 | OpenModScan 写 Holding Register 地址 `0` 后，`dirty=yes`；延迟保存后 `dirty=no`、`active_bank_valid=yes`、`save_count` 增加。 |  |
| MBTCP-14 | Holding Register 重启恢复 | Holding Register 地址 `0` 保存后重启，OpenModScan 读取值仍为保存值。 |  |
| MBTCP-15 | 手动强制保存 | 写 Holding Register 地址 `0` 后执行 `modbus_store_save` 立即保存，重启后值可恢复。 |  |
| MBTCP-16 | Modbus store 清除 | 执行 `modbus_store_clear` 后重启，Holding Register 地址 `0` 恢复默认值 `0`。 |  |
| MBTCP-17 | 延迟保存合并 | 短时间多次写 Holding Register 地址 `0` 后只合并为一次或少量保存，重启后为最后保存值。 |  |
| MBTCP-18 | 双 bank sequence 递增 | 连续两次保存后 `active_sequence` 递增，活动 bank 正常切换或新保存有效。 |  |
| MBTCP-19 | 保存过程中断电恢复 | 保存过程中断电后设备可正常启动，Holding Register 地址 `0` 只能恢复为旧有效值或新有效值，不出现随机值。 |  |
| MBTCP-20 | 多客户端 Modbus TCP | 多个 OpenModScan 连接同时读写时设备不崩溃，已接纳连接通信正常，持久化状态正确。 |  |
| MBTCP-21 | 长时间写入压力测试 | 连续写 Holding Register 地址 `0` 30 分钟设备稳定，`last_error=0` 或错误可解释，重启后持久化值正确。 |  |

## 6. 测试用例

### MBTCP-00 开发寄存器表输入检查

1. 测试目的：

   确认测试组在开始 Modbus TCP 验收前，已经拿到本版本正式 Modbus 寄存器表，避免地址、权限、持久化属性或数据类型理解错误。

2. 前置条件：

   开发人员已发布本轮测试对应的固件版本，并提供配套 Modbus 寄存器表。

3. 测试步骤：

   检查开发人员提供的寄存器表是否至少包含：

   ```text
   寄存器类型
   功能码
   0-based 协议地址
   显示地址
   寄存器名称
   默认值
   数据类型
   单位和比例系数
   读写权限
   是否持久化
   合法范围
   写入副作用
   备注
   ```

   将寄存器表中的基础信息与本文档第 3 章“当前 Modbus 寄存器定义”进行核对。如有差异，向开发人员确认以哪份为准。

4. 预期结果：

   测试组持有的正式寄存器表信息完整，且开发人员已确认该寄存器表与被测固件版本一致。

5. 判定标准：

   寄存器表完整且版本匹配，判定通过。寄存器表缺失、版本不匹配或关键信息不完整时，后续 Modbus 读写测试记录为阻塞。

### MBTCP-01 Modbus TCP 连接检查

1. 测试目的：

   验证封盒整机的 Modbus TCP 服务可通过以太网访问。

2. 前置条件：

   设备已接入网络，PC 可 ping 通设备 mDNS 主机名。

3. 测试步骤：

   打开 OpenModScan，按以下参数连接：

   ```text
   Server/IP: craner-{project}-{type}-{name_uid}.local
   Port: 502
   Unit ID: 1
   ```

   连接后读取 Holding Register 地址 `0`，数量 `1`。

4. 预期结果：

   OpenModScan 能成功连接，读取请求有响应。

5. 判定标准：

   TCP 连接成功且 Unit ID `1` 可通信。

### MBTCP-02 Input Register 基础读取

1. 测试目的：

   验证已定义输入寄存器可读取。

2. 测试步骤：

   使用 OpenModScan：

   ```text
   Function: Read Input Registers, FC04
   Address: 0
   Count: 18
   ```

3. 预期结果：

   读取成功，地址 `0..17` 均返回 16-bit 数值。

4. 判定标准：

   FC04 读取成功，无异常响应。

### MBTCP-03 Holding Register 基础读写

1. 测试目的：

   验证 Holding Register 地址 `0` 可读写。

2. 测试步骤：

   使用 OpenModScan 写入：

   ```text
   Function: Write Single Register, FC06
   Address: 0
   Value: 1234
   ```

   然后读取：

   ```text
   Function: Read Holding Registers, FC03
   Address: 0
   Count: 1
   ```

3. 预期结果：

   读取返回：

   ```text
   Address 0 = 1234
   ```

4. 判定标准：

   写入成功，读取值与写入值一致。

### MBTCP-04 Coil write-only 行为

1. 测试目的：

   验证 Coil 地址 `0` 当前为 write-only，不允许读取；写入后不触发持久化。

2. 测试步骤：

   读取 Coil 地址 `0`：

   ```text
   Function: Read Coils, FC01
   Address: 0
   Count: 1
   ```

   写入 Coil 地址 `0`：

   ```text
   Function: Write Single Coil, FC05
   Address: 0
   Value: ON
   ```

   查询：

   ```text
   modbus_store_status
   ```

3. 预期结果：

   读取 Coil 地址 `0` 应失败，因为它没有 `READABLE` 标志。写入 Coil 地址 `0` 应成功，但它没有 `PERSISTENT` 标志，所以不应触发 `dirty=yes`。

4. 判定标准：

   线圈权限符合 flags 定义，非持久化写入不落盘。

### MBTCP-05 Coil 未定义地址读取

1. 测试目的：

   验证线圈地址空间内未定义地址读取返回 `0`。

2. 测试步骤：

   读取 Coil 地址 `1`：

   ```text
   Function: Read Coils, FC01
   Address: 1
   Count: 1
   ```

3. 预期结果：

   地址 `1` 未定义但在地址空间内，读取成功并返回 OFF/false。

4. 判定标准：

   读取成功，值为 `0`。

### MBTCP-06 Coil 越界访问

1. 测试目的：

   验证 Coil 地址空间外访问失败。

2. 测试步骤：

   读取和写入 Coil 地址 `10`：

   ```text
   Function: Read Coils, FC01
   Address: 10
   Count: 1

   Function: Write Single Coil, FC05
   Address: 10
   Value: ON
   ```

3. 预期结果：

   读取和写入均失败，OpenModScan 显示异常响应或请求失败。

4. 判定标准：

   地址 `10` 超出 `0..9`，不允许读写。

### MBTCP-07 Holding 未定义地址读取

1. 测试目的：

   验证地址空间内未定义 Holding Register 读取返回 `0`。

2. 测试步骤：

   用 OpenModScan 读取 Holding Register：

   ```text
   Function: Read Holding Registers, FC03
   Unit ID: 1
   Address: 1
   Count: 1
   ```

3. 预期结果：

   地址 `1` 在 Holding Register 地址空间 `0..9` 内，但当前未定义，读取成功并返回：

   ```text
   Address 1 = 0
   ```

4. 判定标准：

   读取成功，值为 `0`。

### MBTCP-08 Holding 未定义地址写入失败

1. 测试目的：

   验证地址空间内未定义 Holding Register 不允许写入。

2. 测试步骤：

   用 OpenModScan 写 Holding Register：

   ```text
   Function: Write Single Register, FC06
   Unit ID: 1
   Address: 1
   Value: 123
   ```

   随后执行：

   ```text
   modbus_store_status
   ```

3. 预期结果：

   OpenModScan 应收到异常响应或写入失败；`modbus_store_status` 不应因为这次写入变成 `dirty=yes`。

4. 判定标准：

   未定义地址写入失败，不触发持久化保存。

### MBTCP-09 Holding 越界访问

1. 测试目的：

   验证 Holding Register 地址空间外访问失败。

2. 测试步骤：

   用 OpenModScan 读取：

   ```text
   Function: Read Holding Registers, FC03
   Address: 10
   Count: 1
   ```

   再写入：

   ```text
   Function: Write Single Register, FC06
   Address: 10
   Value: 123
   ```

3. 预期结果：

   读取和写入均失败，OpenModScan 显示异常响应或请求失败。

4. 判定标准：

   地址 `10` 超出 `0..9`，不允许读写。

### MBTCP-10 Input 未定义地址读取

1. 测试目的：

   验证输入寄存器地址空间内未定义地址读取返回 `0`。

2. 测试步骤：

   读取 Input Register 地址 `18`：

   ```text
   Function: Read Input Registers, FC04
   Address: 18
   Count: 1
   ```

3. 预期结果：

   地址 `18` 在地址空间内但未定义，读取成功并返回：

   ```text
   Address 18 = 0
   ```

4. 判定标准：

   读取成功，值为 `0`。

### MBTCP-11 Input 越界读取

1. 测试目的：

   验证输入寄存器地址空间外读取失败。

2. 测试步骤：

   读取 Input Register 地址 `100`：

   ```text
   Function: Read Input Registers, FC04
   Address: 100
   Count: 1
   ```

3. 预期结果：

   读取失败，OpenModScan 显示异常响应或请求失败。

4. 判定标准：

   地址 `100` 超出 `0..99`，不允许读取。

### MBTCP-12 Modbus store 初始状态

1. 测试目的：

   验证 `modbus-store` 初始状态可查询，空分区不会导致系统启动失败。

2. 前置条件：

   如果本轮测试允许清除历史数据，先执行：

   ```text
   modbus_store_clear
   reboot
   ```

3. 测试步骤：

   执行：

   ```text
   modbus_store_status
   ```

4. 预期结果：

   空分区状态下应看到：

   ```text
   initialized=yes
   dirty=no
   active_bank_valid=no
   active_sequence=0
   payload_size=0
   last_error=0
   ```

5. 判定标准：

   系统正常启动，空存储不会报致命错误。

### MBTCP-13 Holding Register 延迟持久化保存

1. 测试目的：

   验证 Holding Register 地址 `0` 写入后会触发 dirty，并延迟保存到外部 `modbus-store`。

2. 测试步骤：

   使用 OpenModScan 写 Holding Register：

   ```text
   Function: Write Single Register, FC06
   Unit ID: 1
   Address: 0
   Value: 4660
   ```

   立即在 Telnet Shell 查询：

   ```text
   modbus_store_status
   ```

   等待至少 `CONFIG_CRANER_MODBUS_REGISTER_STORE_SAVE_DELAY_MS + 1s`，默认约 4 秒，再查询：

   ```text
   modbus_store_status
   ```

3. 预期结果：

   写入后短时间内：

   ```text
   dirty=yes
   ```

   延迟保存完成后：

   ```text
   dirty=no
   active_bank_valid=yes
   active_sequence=1
   payload_size=8
   save_count=1
   last_error=0
   ```

   `payload_size=8` 是因为当前只有 1 条持久化 Holding Register TLV 记录。

4. 判定标准：

   写持久化 Holding Register 能触发 dirty，延迟后自动保存成功。

### MBTCP-14 Holding Register 重启恢复

1. 测试目的：

   验证已保存的 Holding Register 地址 `0` 重启后能自动加载。

2. 前置条件：

   已执行 MBTCP-13，并确认 Holding Register 地址 `0` 保存值为 `4660`。

3. 测试步骤：

   重启设备：

   ```text
   reboot
   ```

   重启后用 OpenModScan 读取 Holding Register：

   ```text
   Function: Read Holding Registers, FC03
   Unit ID: 1
   Address: 0
   Count: 1
   ```

   同时查询：

   ```text
   modbus_store_status
   ```

4. 预期结果：

   Modbus 读取返回：

   ```text
   Address 0 = 4660
   ```

   `modbus_store_status` 显示：

   ```text
   initialized=yes
   active_bank_valid=yes
   active_sequence=1
   load_count>=1
   last_error=0
   ```

5. 判定标准：

   重启后寄存器值恢复为保存值。

### MBTCP-15 手动强制保存

1. 测试目的：

   验证 `modbus_store_save` 可立即保存，不必等待延迟保存。

2. 测试步骤：

   使用 OpenModScan 写 Holding Register 地址 `0`：

   ```text
   Function: Write Single Register, FC06
   Address: 0
   Value: 22136
   ```

   立即执行：

   ```text
   modbus_store_status
   modbus_store_save
   modbus_store_status
   reboot
   ```

   重启后读取 Holding Register 地址 `0`。

3. 预期结果：

   `modbus_store_save` 返回：

   ```text
   status=ok
   ```

   保存后 `dirty=no`，`active_sequence` 比上一次保存增加 `1`。重启后地址 `0` 返回 `22136`。

4. 判定标准：

   手动保存立即生效，重启后可恢复。

### MBTCP-16 Modbus store 清除

1. 测试目的：

   验证 `modbus_store_clear` 会擦除 `modbus-store`，重启后 Modbus persistent 值恢复默认。

2. 前置条件：

   Holding Register 地址 `0` 已保存为非默认值。

3. 测试步骤：

   执行：

   ```text
   modbus_store_clear
   modbus_store_status
   reboot
   modbus_store_status
   ```

   用 OpenModScan 读取 Holding Register 地址 `0`。

4. 预期结果：

   清除后状态：

   ```text
   active_bank_valid=no
   active_sequence=0
   payload_size=0
   dirty=no
   ```

   重启后 Holding Register 地址 `0` 恢复默认值：

   ```text
   Address 0 = 0
   ```

5. 判定标准：

   清除后不再加载旧值。

### MBTCP-17 延迟保存合并

1. 测试目的：

   验证短时间内多次写持久化 Holding Register 会合并为一次或少量保存，避免每次写入都擦写 Flash。

2. 测试步骤：

   查询初始状态：

   ```text
   modbus_store_status
   ```

   记录当前 `save_count` 和 `active_sequence`。

   在小于保存延迟的时间内，连续写 Holding Register 地址 `0` 三次：

   ```text
   Address 0 = 1001
   Address 0 = 1002
   Address 0 = 1003
   ```

   立即查询：

   ```text
   modbus_store_status
   ```

   等待默认约 4 秒后再查询：

   ```text
   modbus_store_status
   ```

   重启后读取 Holding Register 地址 `0`。

3. 预期结果：

   连续写入后 `dirty=yes`。延迟保存后 `dirty=no`，`save_count` 增加 `1`，`active_sequence` 增加 `1`。重启后地址 `0` 为最后一次写入值：

   ```text
   Address 0 = 1003
   ```

4. 判定标准：

   多次快速写入被合并保存，最终值正确。

### MBTCP-18 双 bank sequence 递增

1. 测试目的：

   验证每次成功保存都会写入非活动 bank，并使 sequence 递增。

2. 测试步骤：

   清空：

   ```text
   modbus_store_clear
   ```

   写入并保存第 1 次：

   ```text
   写 Holding Register 地址 0 = 111
   modbus_store_save
   modbus_store_status
   ```

   记录 `active_bank`、`active_sequence`。

   写入并保存第 2 次：

   ```text
   写 Holding Register 地址 0 = 222
   modbus_store_save
   modbus_store_status
   ```

   再记录 `active_bank`、`active_sequence`。

3. 预期结果：

   第 1 次保存后 `active_sequence=1`。第 2 次保存后 `active_sequence=2`。在正常双 bank 切换下，`active_bank` 应在 `0` 和 `1` 之间切换。

4. 判定标准：

   sequence 递增，活动 bank 正常切换或至少新保存有效。

### MBTCP-19 保存过程中断电恢复

1. 测试目的：

   验证保存过程中断电不会导致系统加载损坏数据。

2. 前置条件：

   需要可控电源或继电器，允许在写 Flash 过程中断电。此测试为破坏性稳定性测试。

3. 测试步骤：

   先保存一个已知旧值：

   ```text
   写 Holding Register 地址 0 = 3001
   modbus_store_save
   reboot
   读取 Holding Register 地址 0，确认等于 3001
   ```

   再写入新值：

   ```text
   写 Holding Register 地址 0 = 3002
   ```

   等待接近延迟保存时间，在保存可能发生时断电。重新上电后读取 Holding Register 地址 `0`，并查询：

   ```text
   modbus_store_status
   ```

4. 预期结果：

   设备应正常启动，不应崩溃。读取结果允许为旧有效值 `3001` 或新有效值 `3002`，但不允许随机值、半写入值或无法启动。`modbus_store_status` 不应持续出现致命错误。

5. 判定标准：

   断电后系统仍可启动，并且只加载完整有效 bank。

### MBTCP-20 多客户端 Modbus TCP

1. 测试目的：

   验证多个 Modbus TCP 客户端同时连接时，持久化 Holding Register 写入仍稳定。

2. 前置条件：

   当前 Modbus TCP server 支持固定客户端池，最大客户端数由开发人员在测试前提供。

3. 测试步骤：

   打开多个 OpenModScan 窗口或多个连接会话，同时连接同一台设备。客户端 A 连续读取 Input Register 地址 `0..17`；客户端 B 写 Holding Register 地址 `0`；客户端 C 读取 Holding Register 地址 `0`。同时在 Shell 执行：

   ```text
   modbus_store_status
   ```

4. 预期结果：

   所有已被客户端池接纳的连接应正常通信。写 Holding Register 地址 `0` 后仍会触发 dirty 和保存。超过客户端池数量的连接可能被拒绝或关闭，但不应影响已有连接和系统稳定。

5. 判定标准：

   多客户端场景下系统不崩溃，持久化状态正确。

### MBTCP-21 长时间写入压力测试

1. 测试目的：

   验证长时间写 Modbus persistent 数据时，延迟保存、Flash 擦写和网络服务保持稳定。

2. 前置条件：

   设备正常启动，Modbus TCP 可连接，MobaXterm Telnet Shell 可用。

3. 测试步骤：

   使用 OpenModScan 的轮询写入能力、测试组脚本，或人工辅助脚本每 500ms 写一次 Holding Register 地址 `0`，连续运行 30 分钟。每 1 分钟执行一次：

   ```text
   modbus_store_status
   storage_status
   ```

   压测结束后执行：

   ```text
   modbus_store_save
   reboot
   ```

   重启后读取 Holding Register 地址 `0`。

4. 预期结果：

   设备不崩溃，Modbus TCP 不异常退出，`fail_count` 不增加或保持可解释范围，`last_error=0`。重启后 Holding Register 地址 `0` 为压测最后保存的值或最后一次强制保存值。

5. 判定标准：

   长时间写入过程中系统稳定，最终持久化数据正确。

## 7. 自动化测试建议

1. Telnet 自动化：

   测试脚本可以通过 Telnet 发送命令，并按行匹配关键字段。人工测试时使用 MobaXterm Telnet Session 执行同样命令。例如 `modbus_store_status` 至少匹配：

   ```text
   initialized=yes
   dirty=no
   last_error=0
   ```

2. OpenModScan / Modbus 自动化：

   OpenModScan 测试时需要覆盖以下 Modbus TCP 功能码。如果测试组使用脚本辅助压测，脚本也需要支持这些功能码：

   ```text
   FC01 Read Coils
   FC03 Read Holding Registers
   FC04 Read Input Registers
   FC05 Write Single Coil
   FC06 Write Single Register
   FC15 Write Multiple Coils，可选
   FC16 Write Multiple Registers，可选
   ```

3. 断电测试：

   断电测试需要记录断电时间点、写入值、重启后读取值、`modbus_store_status` 完整输出。预期不是必须恢复到最新值，而是必须恢复到某个完整有效值。

## 8. 缺陷记录建议

1. 如果 Modbus TCP 无法连接，记录设备主机名、设备 IP、OpenModScan 连接参数、ping 结果和 MobaXterm Telnet 状态。

2. 如果读写地址返回值不符合预期，记录 OpenModScan 地址模式是 0-based 还是 1-based，并附上功能码、地址、数量、返回值或异常码。

3. 如果 `modbus_store_save` 失败，记录 `modbus_store_status`、`storage_status`、最近一次 Modbus 写入地址和值。

4. 如果断电测试后读取到随机值，记录旧值、新值、断电时间点、重启后 `active_bank`、`active_sequence`、`payload_size`、`last_error`。

5. 如果多客户端测试中连接被拒绝，先确认是否超过开发人员提供的最大客户端数；未超过时记录为缺陷，超过时记录为预期限制。
