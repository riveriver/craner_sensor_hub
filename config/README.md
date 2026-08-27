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

- 静态 IPv4 或 DHCP 救援地址
- 设备身份服务的公司、型号、产品默认字段

通用能力开关仍放在根目录 `prj.conf`，例如 DHCP 能力、hostname 动态修改能力、服务是否启用等。

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
