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

# 新增產品配置指南

產品配置放在 `config/` 目錄，文件名固定使用：

```text
product_<ProductId>.conf
```

例如產品 ID 是 `101`，配置文件就是：

```text
config/product_101.conf
```

主工程通過 `project_config.json` 的 `Base.ProductId` 選擇當前產品，並由 `Build.ExtraConf` 自動展開成 `config/product_{ProductId}.conf`。

## 1. 複製一份現有配置

通常從最接近的新產品形態複製一份：

```powershell
Copy-Item config\product_101.conf config\product_102.conf
```

然後修改新文件裡的產品 ID、網段、塔機類型、傳感器能力和接口參數。

## 2. 修改設備身份

每份產品配置都要更新產品 ID：

```conf
CONFIG_DEVICE_IDENTITY_DEFAULT_COMPANY="craner"
CONFIG_DEVICE_IDENTITY_DEFAULT_MODEL="sensor-hub"
CONFIG_DEVICE_IDENTITY_DEFAULT_PRODUCT="102"
```

Hostname、mDNS name、MQTT client id 和 MAC 由 `device_identity_service` 根據這些字段和硬件 UID 生成，不要在產品配置裡直接維護舊的 hostname。

## 3. 配置網絡

新建產品配置時，DHCP 默認建議打開：

```conf
CONFIG_NETWORK_MANAGER_DHCP_ENABLE=y
```

網絡模式由 `CONFIG_NETWORK_MANAGER_DHCP_ENABLE` 決定：

```text
CONFIG_NETWORK_MANAGER_DHCP_ENABLE=y -> 使用 DHCP；DHCP 超時失敗後切換救援地址
CONFIG_NETWORK_MANAGER_DHCP_ENABLE=n -> 使用 CONFIG_NETWORK_STATIC_IPV4_* 靜態地址
```

即使 DHCP 打開，也可以保留靜態 IP 配置。這些靜態配置會在關閉 DHCP 時生效：

```conf
CONFIG_NETWORK_MANAGER_DHCP_ENABLE=y
CONFIG_NETWORK_STATIC_IPV4_ADDR="192.168.102.32"
CONFIG_NETWORK_STATIC_IPV4_NETMASK="255.255.255.0"
CONFIG_NETWORK_STATIC_IPV4_GW="192.168.102.1"
```

如果現場要求固定 IP，改成：

```conf
CONFIG_NETWORK_MANAGER_DHCP_ENABLE=n
CONFIG_NETWORK_STATIC_IPV4_ADDR="192.168.102.32"
CONFIG_NETWORK_STATIC_IPV4_NETMASK="255.255.255.0"
CONFIG_NETWORK_STATIC_IPV4_GW="192.168.102.1"
```

救援網段固定保持 `192.168.250.x`：

```conf
CONFIG_NETWORK_DHCP_RESCUE_ENABLE=y
CONFIG_NETWORK_RESCUE_IPV4_ADDR="192.168.250.32"
CONFIG_NETWORK_RESCUE_IPV4_NETMASK="255.255.255.0"
CONFIG_NETWORK_RESCUE_IPV4_GW="192.168.250.1"
```

## 4. 配置塔機類型

平頭塔機：

```conf
CONFIG_TOWER_TYPE_FLAT_TOP=y
```

動臂塔機：

```conf
CONFIG_TOWER_TYPE_LUFFING_JIB=y
```

`REG_CRANE_TYPE` 會根據塔機類型輸出協議 magic number：

```text
未初始化/未選型: 0xFFFF
平頭: 0x00FA
動臂: 0x00DB
```

## 5. 配置產品能力

按產品實際硬件啟用能力：

```conf
CONFIG_ENABLE_SLEWING_ENCODER=y
CONFIG_ENABLE_LUFFING_ENCODER=y
CONFIG_ENABLE_HOISTING_ENCODER=y
CONFIG_ENABLE_LUFFING_IMU=n
CONFIG_ENABLE_ANEMOMETER_SENSOR=y
CONFIG_ENABLE_READ_LOAD_SENSOR=n
```

未啟用能力對應的離線狀態寄存器默認為 `1`，避免上位機誤讀未接入設備的數據。

`REG_DEVICE_CAPABILITY_FLAGS` 使用低 8 位描述能力，高 8 位保留為 `0`：

```text
bit0: 回轉編碼器
bit1: 變幅編碼器
bit2: 起升編碼器
bit3: 風速儀/氣象傳感器
bit4: 載荷傳感器
bit5: 變幅 IMU
bit6: 保留
bit7: 動臂塔機類型標志
```

## 6. 配置接口參數

編碼器：

```conf
CONFIG_ENCODER_SLEWING_IFACE_NAME="rs485-uart7"
CONFIG_ENCODER_LUFFING_IFACE_NAME="rs485-uart8"
CONFIG_ENCODER_HOISTING_IFACE_NAME="rs485-uart4"
CONFIG_ENCODER_MODBUS_UNIT_ID=1
CONFIG_ENCODER_MODBUS_BAUD=9600
CONFIG_ENCODER_MODBUS_RX_TIMEOUT_MS=40
```

變幅 IMU：

```conf
CONFIG_LUFFING_IMU_MODEL_WIT_STANDARD_PRECISION=y
CONFIG_LUFFING_IMU_IFACE_NAME="rs485-uart8"
CONFIG_LUFFING_IMU_MODBUS_UNIT_ID=80
CONFIG_LUFFING_IMU_MODBUS_BAUD=9600
CONFIG_LUFFING_IMU_MODBUS_RX_TIMEOUT_MS=40
```

風速儀：

```conf
CONFIG_ANEMOMETER_IFACE_NAME="rs485-usart6"
CONFIG_ANEMOMETER_MODBUS_UNIT_ID=1
CONFIG_ANEMOMETER_MODBUS_BAUD=9600
CONFIG_ANEMOMETER_MODBUS_RX_TIMEOUT_MS=100
```

只保留當前產品會用到的接口配置，避免同一個 RS485 口被兩類業務同時使用。

## 7. 切換當前產品

修改 `project_config.json`：

```json
{
  "Base": {
    "ProductId": 102
  },
  "Build": {
    "ExtraConf": "config/product_{ProductId}.conf"
  }
}
```

如果 OTA 目標也隨產品網段變化，記得同步修改：

```json
{
  "Base": {
    "OtaTarget": "192.168.102.32"
  }
}
```

## 8. 重新配置並構建

產品配置變更後，要重新跑 pristine 構建，確保新的 `product_<ProductId>.conf` 被 CMake/Kconfig 重新合併：

```powershell
powershell -ExecutionPolicy Bypass -File .\dev.ps1 build -target all -pristine
```

構建日志中應能看到：

```text
Merged configuration '...\config\product_102.conf'
```

也可以檢查生成配置：

```powershell
Select-String -Path build\craner_general_stm32h743vit6\craner_sensor_hub\zephyr\.config -Pattern "CONFIG_NETWORK_MANAGER_DHCP_ENABLE|CONFIG_NETWORK_STATIC_IPV4|CONFIG_NETWORK_RESCUE_IPV4|CONFIG_TOWER_TYPE"
```

## 9. 新產品配置檢查清單

- 文件名是 `config/product_<ProductId>.conf`。
- `CONFIG_DEVICE_IDENTITY_DEFAULT_PRODUCT` 已改成新產品 ID。
- `CONFIG_NETWORK_MANAGER_DHCP_ENABLE` 已明確配置。
- 靜態 IP、網關和 OTA 目標使用正確產品網段。
- 救援 IP 仍在 `192.168.250.x` 網段。
- 塔機類型只選一個。
- 已按硬件實際情況配置編碼器、IMU、風速儀和載荷能力。
- RS485 接口名稱沒有被互斥業務重複占用。
- 已執行 `build -target all -pristine` 驗證。

