# 测试用例总览

本目录用于维护 `craner_sensor_hub` 的功能测试、回归测试和自动化测试资料。测试用例按功能域组织，不按 P0-P3 分级。

## 目录

```text
test/
├── README.md
├── test_cases/
├── automation/
├── fixtures/
└── reports/
```

## 功能清单

| 功能域 | 测试文档 | 主要实现位置 |
| --- | --- | --- |
| 参数与持久化 | [参数与持久化测试用例.md](test_cases/参数与持久化测试用例.md) | `module/device_param_server`、`module/modbus_data_model` |
| 设备身份 | [设备身份测试用例.md](test_cases/设备身份测试用例.md) | `module/device_identity_service` |
| 网络管理 | [网络管理测试用例.md](test_cases/网络管理测试用例.md) | `module/network_manager_service` |
| Shell 与 MQTT 后端 | [Shell测试用例.md](test_cases/Shell测试用例.md) | Zephyr `SHELL_BACKEND_MQTT`、`module/device_identity_service` |
| 以太网压力与老化 | [以太网压力与老化测试用例.md](test_cases/以太网压力与老化测试用例.md) | `module/network_manager_service`、网络硬件 |
| 以太网性能 | [以太网性能测试用例.md](test_cases/以太网性能测试用例.md) | 网络接口、TCP/IP 协议栈 |
| 固件升级 | [固件升级测试用例.md](test_cases/固件升级测试用例.md) | `module/ota_manager_service`、MCUboot |
| 系统健康 | [系统健康测试用例.md](test_cases/系统健康测试用例.md) | `app/system_health`、`module/system_health_service` |
| 故障转储 | [故障转储测试用例.md](test_cases/故障转储测试用例.md) | `module/fault_dump_service` |
| 时间管理 | [时间管理测试用例.md](test_cases/时间管理测试用例.md) | `module/time_manager_service` |
| Modbus TCP | [Modbus TCP测试用例.md](<test_cases/Modbus TCP测试用例.md>) | `module/modbus_tcp_server`、`app/modbus_tcp` |
| Modbus TCP 压力与老化 | [Modbus TCP压力与老化测试用例.md](<test_cases/Modbus TCP压力与老化测试用例.md>) | `module/modbus_tcp_server`、`app/modbus_tcp` |
| 编码器 | [编码器测试用例.md](test_cases/编码器测试用例.md) | `module/encoder_sample_service`、`app/encoder_app` |
| 惯性测量单元 | [惯性测量单元测试用例.md](test_cases/惯性测量单元测试用例.md) | `module/imu_sample_service`、`app/luffing_imu_app` |
| 风速仪 | [风速仪测试用例.md](test_cases/风速仪测试用例.md) | `module/anemometer_sample_service`、`app/anemometer_app` |
| 电源管理 | [电源管理测试用例.md](test_cases/电源管理测试用例.md) | `app/power_manager` |

## 测试工具

- MobaXterm：Telnet Shell
- OpenModScan 或同类工具：Modbus TCP
- `ping`、ARP、Wireshark：网络连通性和报文分析
- `mcumgr`：OTA 镜像上传和状态确认
- MQTTX：仅在产品配置启用 MQTT 时使用

## 开发者必须提供的输入

- 固件版本、板卡型号和测试构建配置
- 设备 hostname、IP 获取方式、Telnet 账号和端口
- Modbus TCP 端口、Unit ID、地址基准、最新寄存器表
- OTA 镜像、镜像版本、升级限制和回滚预期
- 传感器模拟器或实物、串口参数、正常值范围
- 是否允许清除参数、持久化数据和 Fault Dump
- MQTT broker、账号、topic 和 payload 格式（如启用）

## 结果状态

- `通过`：实际结果符合预期。
- `失败`：实际结果与预期不符，必须记录缺陷编号。
- `阻塞`：环境、设备、工具或账号问题导致无法执行。
- `不适用`：当前产品配置没有该功能或测试条件。

## 通用记录要求

每条用例必须填写测试时间、设备 hostname/IP、固件版本、实际结果和状态。失败时附上命令输出或工具截图、输入参数、期望值、实际值及复现次数。

各文档中的地址、阈值、账号和 topic 以当前发布版本的开发者输入为准；文档与发布寄存器表不一致时，以发布寄存器表为准。
