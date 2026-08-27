# 产品配置片段

`config/` 用于放置不同项目、不同现场网络环境的 Kconfig 片段。

主工程默认加载：

```text
config/product_{ProductId}.conf
```

当前项目的 `ProductId` 是 `18`，因此 `./dev build` 会加载：

```text
config/product_18.conf
```

该文件只放产品差异参数，例如：

- 是否使用 DHCP、静态 IPv4、网关、DHCP 救援地址
- 设备身份服务的公司、型号、产品默认字段
- NTP 服务器、时区等现场时间显示参数
- 编码器、IMU、风速计等现场设备的 RS485 接口名、Modbus 从站 ID、波特率和超时，配置项超时单位统一使用 ms
- 塔机类型，以及编码器、IMU、风速计、吊重等业务是否启用

通用基础能力和所有项目共用的默认策略仍放在根目录 `prj.conf`，例如 Zephyr 子系统、Shell、日志、网络协议栈、存储、OTA、健康管理框架、Modbus TCP 默认行为、健康检测阈值和服务容量等。
只有具体产品才决定的业务组合、现场设备参数和现场网络地址放在 `config/product_*.conf`。

切换项目配置时，优先修改 `project_config.json`：

```json
{
  "Base": {
    "ProductId": 18
  },
  "Build": {
    "ExtraConf": "config/product_{ProductId}.conf"
  }
}
```

旧的 `CONFIG_NET_HOSTNAME="..."` 不再由产品配置直接维护。运行时 hostname 统一由 `device_identity_service` 根据设备身份生成。
