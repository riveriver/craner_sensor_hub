# Modbus 寄存器服务

`modbus_register_service` 是可复用的 Modbus 寄存器表服务。它只负责寄存器数据模型和读写 API，不负责 TCP、RTU、产品寄存器定义或业务逻辑。

## 负责范围

- 定义 coil、input register、holding register 的通用结构。
- 注册产品提供的 `struct modbus_register_map`。
- 初始化默认值。
- 按地址或名称读写寄存器。
- 支持连续范围读写。
- 校验地址空间、读写权限和持久化标记。
- 可选保存持久化 coil/holding register 到 flash。

## 不负责范围

- 不启动 Modbus TCP server。
- 不启动 Modbus RTU server。
- 不定义产品寄存器表。
- 不知道传感器、塔机类型、协议版本等业务字段。

## 文件结构

```text
module/modbus_register_service/
  CMakeLists.txt
  Kconfig
  README.md
  modbus_register_service.conf
  modbus_register_map.h
  modbus_register_service.h
  modbus_register_service.c
  modbus_register_store.h
  modbus_register_store.c
```

## CMake 集成

```cmake
add_subdirectory(module/modbus_register_service)

target_link_libraries(app PRIVATE
    modbus_register_service
)
```

## 常用配置

```conf
CONFIG_MODBUS_REGISTER_SERVICE=y
```

启用持久化：

```conf
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_MODBUS_REGISTER_STORE=y
CONFIG_MODBUS_REGISTER_STORE_SAVE_DELAY_MS=3000
```

当前持久化后端要求 DTS 中存在：

```dts
modbus_store_partition: partition@... {
    label = "modbus-store";
};
```

## 产品侧使用

产品工程定义自己的寄存器表：

```c
static struct modbus_register_input input_register_table[] = {
    {
        .name = "REG_SLEWING_TIMESTAMP_H",
        .addr = 0x0010,
        .default_value = 0,
        .flags = MODBUS_REG_ACCESS_RW,
    },
};

static struct modbus_register_map app_register_map = {
    .inputs = input_register_table,
    .input_count = ARRAY_SIZE(input_register_table),
    .input_address_size = 100,
};
```

然后注册：

```c
modbus_register_service_register_map(&app_register_map);
modbus_register_service_init();
```

业务模块写寄存器：

```c
modbus_register_service_write_inputs_by_name(
    "REG_SLEWING_TIMESTAMP_H", values, ARRAY_SIZE(values));
```
