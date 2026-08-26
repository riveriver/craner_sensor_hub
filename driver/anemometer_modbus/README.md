# 风速计 Modbus Helper

`anemometer_modbus` 只负责当前风速计的 Modbus RTU 读取和数据解析。

## 负责范围

- 在指定 RS485/Modbus 接口上初始化 Zephyr Modbus RTU client。
- 读取配置的保持寄存器块。
- 解析温度、湿度、气压、风速和风向字段。
- 导出 `anemometer_modbus_backend`，供 `anemometer_sample_service` 调用。

## 不负责范围

- 不决定产品是否启用风速计。
- 不创建采样线程。
- 不维护采样统计或失败重试策略。
- 不写 Modbus TCP 寄存器。
- 不更新系统健康事件。

启用配置：`CONFIG_ANEMOMETER_MODBUS`。
