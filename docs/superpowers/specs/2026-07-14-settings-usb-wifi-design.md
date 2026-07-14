# 设置、USB MSC 与 WiFi 配置架构设计

## 目标

在保持“每个功能独立 ESP-IDF image、运行时互不共存”的前提下，新增 Launcher 设置子页、USB MSC App 和 WiFi Setup App。USB App 独占 EPD、按键、SDMMC 与 TinyUSB；WiFi App 独占 EPD、按键、WiFi 与凭据配置。Launcher 只负责显示入口和切换分区，不初始化 SD、USB 或 WiFi。

本阶段同时固定 16MB Flash 的分区 ABI。后续普通 App 使用预留 slot，不再移动已有分区。

## 不迁移范围

不得迁移或重新实现以下旧架构：

- `ink_system_runtime`
- `ink_system_services`
- `ink_resource_coordinator`
- `ink_usb_msc_coordinator`
- `ink_wifi_coordinator`
- 旧 app registry / runtime shell
- MPU、tilt 输入与相关任务
- WiFi 在 Launcher、Reader、Photo 中的自动初始化
- USB MSC 在 Launcher、Reader、Photo 中的常驻服务

## 固定分区 ABI

系统分区保持当前地址，避免现有 NVS、OTA data 和 PHY 区域重解释。WiFi 凭据使用新增的独立 NVS 分区。

| Label | Type/Subtype | Offset | Size | 用途 |
|---|---|---:|---:|---|
| `nvs` | data/nvs | `0x009000` | `0x006000` | 系统 NVS，保持不动 |
| `otadata` | data/ota | `0x00F000` | `0x002000` | boot selection，保持不动 |
| `phy_init` | data/phy | `0x011000` | `0x001000` | PHY，保持不动 |
| `wifi_nvs` | data/nvs | `0x012000` | `0x00C000` | WiFi 配置专用 NVS |
| `nvs_keys` | data/nvs_keys | `0x01E000` | `0x001000` | 预留 NVS 加密密钥区，本阶段不启用 |
| `launcher` | app/factory | `0x020000` | `0x200000` | Launcher 与设置导航 |
| `reader` | app/ota_0 | `0x220000` | `0x200000` | Reader |
| `photo` | app/ota_1 | `0x420000` | `0x200000` | Photo |
| `usb_msc` | app/ota_2 | `0x620000` | `0x200000` | USB MSC |
| `wifi_setup` | app/ota_3 | `0x820000` | `0x200000` | WiFi Setup |
| `future_a` | app/ota_4 | `0xA20000` | `0x200000` | 固定未来 App slot |
| `future_b` | app/ota_5 | `0xC20000` | `0x200000` | 固定未来 App slot |
| `data` | data/fat | `0xE20000` | `0x1E0000` | 内部数据保留区 |

所有 App slot 统一为 2MB，避免 ESP-IDF 的“最小 App 分区”构建检查把其他 App 限制在更小的 Launcher slot。普通 App 必须控制在 2MB 内；大型模型、照片、书籍和音频数据放入 SD 或 data，不扩大 App slot。

`future_a`、`future_b` 的 label 和 offset 永久保留。未来添加 App 时只新增语义 boot switch 和烧录脚本，不重命名 slot、不修改分区表。

应用分区位置会在本阶段一次性变化，因此首次上板必须完整写入 bootloader、partition table、otadata 和五张实际 App image。后续单 App 开发仍只写目标 slot。

## Launcher 设置子页

Launcher 只保留两个静态页面状态，不引入 app manager：

```text
MAIN                       SETTINGS
书库                       U 盘模式
相册                       WiFi 配置
设置
```

在 MAIN 页面选择“设置”进入 SETTINGS 页面，不重启。SETTINGS 页面按 Back 返回 MAIN。选择 U 盘或 WiFi 后显示现有 `Now Loading...` Flash 图片，分别切换到 `usb_msc` 或 `wifi_setup` 分区。

当前按键语义映射为：

- `INK_BUTTON_LEFT / GPIO12`：上
- `INK_BUTTON_RIGHT / GPIO11`：下
- Confirm：进入/选择
- Back：返回上一级

未来焊接真正的左右键后，在输入语义层新增横向动作，不修改 Launcher、WiFi 状态机的数据模型。

## USB MSC App

### 组件边界

新增 `ink_usb_msc_core`，只负责：

- 初始化 TinyUSB device 与 MSC driver。
- 以 raw SDMMC card 创建 MSC storage。
- 处理 attach、detach、storage mount 与错误事件。
- 启动导出、停止导出并释放 SDMMC/TinyUSB。
- 暴露小型状态枚举：starting、waiting、active、stopping、error。

旧 `ink_usb_msc_service.c` 中 TinyUSB 配置、事件处理和 `tinyusb_msc_new_storage_sdmmc()` 可作为参考；必须移除 `ink_system_services`、`ink_app_boot`、runtime 和 coordinator 依赖。USB App 不挂载 FATFS，不在导出期间读取 SD 文件。

### 启动和退出

USB App 启动顺序：

1. 打印 `APP_START name=usb_msc`。
2. 初始化 EPD 与按键；保留 Launcher 切换前画出的 Loading，不先显示独立 USB 页面。
3. 初始化 raw SDMMC、TinyUSB 与 MSC storage。
4. 导出成功后直接用固定居中区域局刷 Flash 内置的 `USB_mode_active.bmp`。
5. 导出失败时直接用同一区域局刷 `USB_mode_failed.bmp`，不得显示成功图片。

两张 USB 图片均为 200x40、1bpp。USB App 不绘制占位首页或状态卡片；进入 App 后用户只看到 Loading 被最终的 Active/Failed 弹窗替换。

Back 使用短按，但必须先停止 MSC、删除 storage、关闭 SD card，再显示 Loading 并返回 Launcher。停止失败时留在 USB App，复用 `USB_mode_failed.bmp` 显示错误，不能带着活动 MSC 直接重启。用户操作上仍应先在电脑端安全弹出 U 盘，但本阶段不增加独立说明页面或额外素材。

## WiFi Setup App

### 组件边界

- `ink_wifi_core`：ESP-NETIF、event loop、WiFi STA 初始化、异步扫描、连接、断开和 IP 结果。
- `ink_wifi_config`：单活动 WiFi 的 `wifi_nvs` 持久化 API。
- `ink_wifi_setup_core`：纯状态机、AP 排序、列表选择、键盘游标、输入文本和页面转换。
- `ink_wifi_setup_ui`：纯 framebuffer renderer 与局刷区域计算。
- `apps/wifi_setup/main/app_main.c`：初始化、事件循环和薄胶水。

旧 `ink_wifi_setup_state/input/ui` 中不依赖 runtime 的 AP 列表、键盘模型和绘制逻辑可拆出迁移。旧 `ink_wifi_setup_app.c`、`ink_wifi_coordinator`、MPU/tilt 事件和后台任务框架不得整体复制。

### 页面状态

```text
STARTING -> SCANNING -> AP_LIST -> PASSWORD_KEYBOARD
                                      |
                                      v
                                 CONNECTING
                                  /      \
                              SUCCESS    ERROR
```

WiFi App 初始化 EPD 与按键后，先用固定居中区域局刷 `WiFi_Scanning.bmp`，随后立即启动异步扫描，避免进入 App 后长时间没有屏幕反馈。扫描失败显示 `WiFi_Scan_Failed.bmp`；扫描成功但没有 AP 时显示 `No_WiFi_Found.bmp`。扫描结果按信号强度排序并对 SSID 去重；有结果时才全页切换到动态 AP_LIST。AP_LIST 使用上/下选择，Confirm 进入密码页；开放网络可直接进入 CONNECTING。

PASSWORD_KEYBOARD 完整渲染旧版小写、大写、符号三层键盘。当前只有纵向导航可用，因此 `GPIO12/GPIO11` 按 row-major 顺序向前/向后遍历全部键，支持循环；Confirm 激活字符或 `space/delete/clear/layer/connect`。长按允许连续移动以降低临时输入成本。Back 从键盘返回 AP_LIST，AP_LIST Back 返回 Launcher。

提交密码后连接选中的 SSID，等待 `IP_EVENT_STA_GOT_IP`，超时为 15 秒：

- 开始连接时局刷 `WiFi_Connecting....bmp`。
- 成功获取 IP 后才保存 NVS，并局刷 `WiFi_Saved.bmp`。
- 失败或超时不覆盖已有活动配置，保留当前输入并局刷 `WiFi_Connect_Failed.bmp`。
- SSID 可以记录到日志；密码、密码长度和明文输入不得记录到日志。

WiFi 的 AP 列表、SSID、密码掩码、键盘字符、层切换和操作键必须由 framebuffer renderer 动态绘制，不制作成静态图片。

### 固定状态素材

以下素材位于工作区 `素材/`，全部为 200x40、1bpp BMP。构建时机械转换为 Flash 常量，运行时不得读取文件系统或 SD：

| 状态 | 素材文件 |
|---|---|
| USB 导出成功 | `USB_mode_active.bmp` |
| USB 导出失败 | `USB_mode_failed.bmp` |
| WiFi 扫描中 | `WiFi_Scanning.bmp` |
| WiFi 扫描失败 | `WiFi_Scan_Failed.bmp` |
| 未发现 WiFi | `No_WiFi_Found.bmp` |
| WiFi 连接中 | `WiFi_Connecting....bmp` |
| WiFi 已保存 | `WiFi_Saved.bmp` |
| WiFi 连接失败 | `WiFi_Connect_Failed.bmp` |

本阶段不让 Launcher、Reader、Photo 自动连接 WiFi。它们未来需要联网时显式调用共享配置 API并自行初始化 `ink_wifi_core`。

## 单活动 WiFi NVS 契约

公开 API 从第一版保持稳定：

```c
typedef struct {
  char ssid[33];
  char password[65];
} ink_wifi_credentials_t;

esp_err_t ink_wifi_config_load_active(ink_wifi_credentials_t *out);
esp_err_t ink_wifi_config_save_active(
    const ink_wifi_credentials_t *credentials);
esp_err_t ink_wifi_config_clear_active(void);
```

组件使用 `nvs_flash_init_partition("wifi_nvs")` 和 `nvs_open_from_partition("wifi_nvs", "ink_wifi", ...)`。内部至少保存 `schema_version`、`ssid`、`password`，写入后调用 `nvs_commit()`。输入长度非法时拒绝保存；读取失败时清零输出。密码绝不出现在日志、错误文本或断言中。

以后支持多个网络时，内部 schema 可以升级为列表，但 `load_active/save_active/clear_active` 的含义和签名保持不变。

本阶段不启用 NVS encryption。`nvs_keys` 仅为将来启用 Flash/NVS 加密预留，避免再次修改分区 ABI。

## 刷新策略

- Launcher MAIN/SETTINGS 选择变化继续使用区域局刷。
- App 切换继续先局刷 `Now Loading...`，再重启。
- USB Active/Failed 图片共用现有 Loading 的固定居中小区域，并使用快速区域局刷。
- WiFi 固定状态图片共用同一居中区域并使用快速区域局刷；AP 列表选择和键盘游标使用区域局刷；进入动态 AP 列表、键盘等整页页面时全刷。
- USB/WiFi App 返回 Launcher 前均显示 Loading。

## 日志约束

新增启动日志：

```text
APP_START name=usb_msc
APP_START name=wifi_setup
```

新增切换日志：

```text
BOOT_SWITCH from=launcher to=usb_msc
BOOT_SWITCH from=launcher to=wifi_setup
BOOT_SWITCH from=usb_msc to=launcher
BOOT_SWITCH from=wifi_setup to=launcher
```

USB 日志只允许出现在 USB App；WiFi 初始化、扫描、连接日志只允许出现在 WiFi App。任何 App 均不得打印 WiFi 密码。

## 验收标准

1. 新分区表精确结束于 16MB，所有 App offset/size 对齐且固定。
2. Launcher MAIN 可进入 SETTINGS，SETTINGS 可返回 MAIN。
3. SETTINGS 可切换并启动 USB MSC 与 WiFi Setup 独立固件。
4. USB App 能被电脑识别为 MSC，SD 内容可读写；active 后显示指定图片。
5. USB Back 先停止 MSC 再返回 Launcher，失败时不重启。
6. WiFi App 首屏先显示扫描状态，随后列出附近 AP。
7. 当前两枚方向键按上/下语义选择 AP，并可线性遍历完整键盘。
8. 密码输入后能连接并获取 IP；成功后保存单活动配置到 `wifi_nvs`。
9. 连接失败不覆盖原配置；重启 WiFi App 后可以读取已保存配置。
10. Launcher、Reader、Photo 不出现 WiFi、TinyUSB、MSC 初始化日志。
11. 不引入旧 runtime、resource coordinator、WiFi coordinator、USB coordinator 或 MPU/tilt 依赖。

## 实施顺序

本阶段拆成三个可独立验证的里程碑：

1. 固定分区 ABI、Launcher 设置子页、两个最小独立 App 和 boot switch 闭环。
2. USB MSC core、USB App、成功图片和电脑端读写/退出验证。
3. WiFi scan/connect、键盘 UI、单活动 NVS 保存和重启读取验证。

每个里程碑先运行相关 host/contract test并构建受影响 App；仅第三个里程碑结束时运行完整 host、pytest、禁止依赖扫描和 `build_all`。首次采用新分区表时执行一次 COM9 全布局烧录，后续只写受影响 slot。
