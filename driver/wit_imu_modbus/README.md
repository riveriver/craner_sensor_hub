# WIT IMU Modbus Helper

`wit_imu_modbus` 只负责 WIT IMU 的 Modbus RTU 协议细节和角度解析。

## 负责范围

- 在指定 RS485/Modbus 接口上初始化 Zephyr Modbus RTU client。
- 读取 WIT 标准精度角度寄存器。
- 读取 WIT 高精度角度寄存器。
- 输出 roll、pitch、yaw 的原始值和毫度值。
- 导出 `wit_imu_modbus_backend`，供 `imu_sample_service` 调用。

## 不负责范围

- 不创建采样线程。
- 不维护采样统计或失败重试策略。
- 不注册 shell 命令。
- 不写 Modbus TCP 寄存器。
- 不更新系统健康事件。
- 不处理产品安装位置、供电或板级 wiring。

启用配置：`CONFIG_WIT_IMU_MODBUS`。
