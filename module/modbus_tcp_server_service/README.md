# Modbus TCP Server 服务

`modbus_tcp_server_service` 是可复用的 Modbus TCP 协议入口。它负责 TCP 监听、客户端管理和 Modbus TCP 到 Zephyr Modbus RAW server 的转发。

## 负责范围

- 监听 Modbus TCP 端口，默认 `502`。
- 管理固定数量的 TCP client。
- 读取 MBAP header 和 PDU data。
- 调用 Zephyr Modbus RAW server 处理请求。
- 将 RAW 响应重新封装并发回 TCP client。
- 通过 `modbus_register_service` 完成 coil/input/holding 读写。

## 不负责范围

- 不定义产品寄存器表。
- 不保存寄存器。
- 不处理 Modbus RTU。
- 不知道产品业务、传感器通道或系统健康事件。

## 文件结构

```text
module/modbus_tcp_server_service/
  CMakeLists.txt
  Kconfig
  README.md
  modbus_tcp_server_service.conf
  modbus_tcp_server_service.c
```

## 依赖

- `CONFIG_MODBUS=y`
- `CONFIG_NET_TCP=y`
- `CONFIG_NET_SOCKETS=y`
- `CONFIG_POSIX_API=y`
- `CONFIG_MODBUS_RAW_ADU=y`
- `CONFIG_MODBUS_REGISTER_SERVICE=y`

## CMake 集成

```cmake
add_subdirectory(module/modbus_register_service)
add_subdirectory(module/modbus_tcp_server_service)

target_link_libraries(app PRIVATE
    modbus_register_service
    modbus_tcp_server_service
)
```

## 常用配置

```conf
CONFIG_MODBUS_REGISTER_SERVICE=y
CONFIG_MODBUS_TCP_SERVER_SERVICE=y
CONFIG_MODBUS_TCP_SERVER_PORT=502
CONFIG_MODBUS_TCP_SERVER_MAX_CLIENTS=4
```

开发诊断时可打开示例自定义功能码：

```conf
CONFIG_MODBUS_TCP_SERVER_CUSTOM_FC_EXAMPLE=y
```

量产固件默认建议关闭该示例功能码。
