# IDECODER 编码器 Modbus Helper

`idecoder_encoder_modbus` 只负责 IDECODER 编码器的 Modbus RTU 访问和数据解析。

## 负责范围

- 在指定 RS485/Modbus 接口上初始化 Zephyr Modbus RTU client。
- 读取 IDECODER 编码器保持寄存器。
- 解析圈数和单圈值。
- 导出 `idecoder_encoder_modbus_backend`，供 `encoder_sample_service` 调用。

## 不负责范围

- 不决定回转、变幅、起升等业务通道名称。
- 不创建采样线程。
- 不维护采样统计或失败重试策略。
- 不写 Modbus TCP 寄存器。
- 不更新系统健康事件。

启用配置：`CONFIG_IDECODER_ENCODER_MODBUS`。
