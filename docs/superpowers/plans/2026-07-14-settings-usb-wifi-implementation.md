# 设置、USB MSC 与 WiFi 配置 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 固定 16MB Flash 分区 ABI，并以独立 ESP-IDF App image 实现 Launcher 设置入口、USB MSC 和可保存单活动凭据的 WiFi 配置。

**Architecture:** Launcher 只维护 MAIN/SETTINGS 两页导航并切换 boot partition；`usb_msc`、`wifi_setup` 各自独占运行时资源，与 Reader/Photo 不共存。共享代码按 `ink_usb_msc_core`、`ink_wifi_core`、`ink_wifi_config`、`ink_wifi_setup_core` 和 `ink_wifi_setup_ui` 拆分，不引入旧 runtime、coordinator 或 MPU 输入。

**Tech Stack:** ESP-IDF 5.5.4、ESP32-S3、C、TinyUSB MSC、SDMMC、ESP WiFi STA、NVS、1bpp EPD framebuffer、PowerShell、pytest/host C test、COM9。

---

## 里程碑 1：固定分区与 App 切换闭环

### Task 1: 固定分区 ABI 和分区契约测试

**Files:**
- Modify: `partitions/partitions_16mb.csv`
- Create: `tools/tests/test_partition_contract.py`

- [ ] **Step 1: 写失败测试**

测试解析 CSV 并逐项断言 label、type/subtype、offset、size 与设计规格一致；断言所有分区连续、不重叠、App offset 为 `0x10000` 对齐，最终结束地址为 `0x1000000`，所有 App slot 均为 `0x200000`。

- [ ] **Step 2: 验证 RED**

Run: `python -m pytest tools/tests/test_partition_contract.py -q`

Expected: 旧分区表缺少 `wifi_nvs/nvs_keys/usb_msc/wifi_setup/future_a/future_b`，测试失败。

- [ ] **Step 3: 写入固定布局**

使用规格中批准的布局：系统分区保持 `0x9000..0x12000`，`wifi_nvs` 位于 `0x12000`、`nvs_keys` 位于 `0x1E000`；Launcher/Reader/Photo/USB/WiFi/Future A/Future B 从 `0x20000` 起依次使用 2MB slot；data 从 `0xE20000` 到 16MB 末尾。

- [ ] **Step 4: 验证 GREEN**

Run: `python -m pytest tools/tests/test_partition_contract.py -q`

Expected: PASS。

### Task 2: Boot switch 增加 USB 与 WiFi 路由

**Files:**
- Modify: `components/ink_boot_switch/include/ink_boot_switch.h`
- Modify: `components/ink_boot_switch/ink_boot_switch.c`
- Modify: `tools/tests/test_app_startup_contracts.py`

- [ ] **Step 1: 写失败契约测试**

断言头文件公开：

```c
esp_err_t ink_boot_switch_to_usb_msc(void);
esp_err_t ink_boot_switch_to_wifi_setup(void);
```

并断言实现分别调用现有 `switch_to("usb_msc")` 和 `switch_to("wifi_setup")`。

- [ ] **Step 2: 验证 RED**

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k boot_switch_settings_targets -q`

Expected: 缺少两个 API，测试失败。

- [ ] **Step 3: 实现薄路由**

只增加两个公开函数，复用现有 `esp_partition_find_first()`、`esp_ota_set_boot_partition()`、`esp_restart()` 路径，不增加 app registry 或 manager。

- [ ] **Step 4: 验证 GREEN**

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k boot_switch_settings_targets -q`

Expected: PASS。

### Task 3: Launcher MAIN/SETTINGS 两页导航

**Files:**
- Modify: `components/ink_epd_ui/include/ink_epd_ui.h`
- Modify: `components/ink_epd_ui/ink_epd_ui.c`
- Modify: `components/ink_epd_ui/ink_ui_text_assets.c`
- Modify: `components/ink_epd_ui/test/loading_ui_host_test.c`
- Modify: `apps/launcher/main/app_main.c`
- Modify: `tools/tests/test_app_startup_contracts.py`

- [ ] **Step 1: 写失败 UI 与启动契约测试**

Host test 覆盖 MAIN 三项和 SETTINGS 两项的 marker、文字/图标、页面边界与选择局刷区域；pytest 断言 Launcher 不初始化 SD/WiFi/USB，MAIN Confirm 的设置项只切页面，SETTINGS Back 返回 MAIN，USB/WiFi 路由先显示 Loading，再打印 `BOOT_SWITCH` 并调用对应 boot switch。

- [ ] **Step 2: 验证 RED**

Run: `components/ink_epd_ui/test/run_host_tests.ps1`

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k launcher_settings -q`

Expected: 新页面 API/状态尚不存在，失败。

- [ ] **Step 3: 实现两个静态页面**

新增简单页面枚举和固定数量选择：MAIN 为书库/相册/设置，SETTINGS 为 U 盘模式/WiFi 配置。继续使用当前两枚方向键作为上/下，页面内选择变化仅刷新 marker 区域；SETTINGS Back 返回 MAIN 并全页刷新。绘制沿用 Launcher 的任务栏、70px 行、图标左侧 marker、Flash 固化中文文字和底部分隔线，不加载 SD 字体。

- [ ] **Step 4: 接入切换日志与 Loading**

USB 路由顺序固定为：

```c
show_boot_loading(framebuffer, "launcher", "usb_msc");
ESP_LOGI(TAG, "BOOT_SWITCH from=launcher to=usb_msc");
(void)ink_boot_switch_to_usb_msc();
```

WiFi 路由同样使用 `wifi_setup`。切页操作不得打印 boot switch。

- [ ] **Step 5: 验证 GREEN**

Run: `components/ink_epd_ui/test/run_host_tests.ps1`

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k "launcher_settings or boot_switch_settings_targets" -q`

Expected: PASS。

### Task 4: 两个最小独立 ESP-IDF App

**Files:**
- Create: `apps/usb_msc/CMakeLists.txt`
- Create: `apps/usb_msc/sdkconfig.defaults`
- Create: `apps/usb_msc/main/CMakeLists.txt`
- Create: `apps/usb_msc/main/app_main.c`
- Create: `apps/wifi_setup/CMakeLists.txt`
- Create: `apps/wifi_setup/sdkconfig.defaults`
- Create: `apps/wifi_setup/main/CMakeLists.txt`
- Create: `apps/wifi_setup/main/app_main.c`
- Modify: `tools/tests/test_app_startup_contracts.py`

- [ ] **Step 1: 写失败启动契约**

断言两个项目使用共享 `EXTRA_COMPONENT_DIRS`、16MB 自定义分区表和 ESP32-S3/PSRAM 配置；最小 App 分别打印 `APP_START name=usb_msc` / `APP_START name=wifi_setup`，初始化 EPD/按键、显示明确占位状态，Back 先显示 Loading，再打印返回日志并调用 `ink_boot_switch_to_launcher()`。断言源码不含旧 runtime/coordinator、MPU、WiFi/USB 实际初始化。

- [ ] **Step 2: 验证 RED**

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k minimal_settings_apps -q`

Expected: 两个 App 尚不存在，失败。

- [ ] **Step 3: 创建最小 App**

两个 `app_main.c` 保持薄：分配 PSRAM framebuffer、初始化 EPD/按键、全刷占位页、轮询 Back。USB 显示 `USB MSC / NOT READY`，WiFi 显示 `WIFI SETUP / NOT READY`；本里程碑不初始化 SDMMC、TinyUSB、WiFi、NVS。

- [ ] **Step 4: 验证 GREEN**

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k minimal_settings_apps -q`

Expected: PASS。

### Task 5: 烧录脚本与首次全布局写入

**Files:**
- Modify: `tools/flash_reader.ps1`
- Modify: `tools/flash_photo.ps1`
- Create: `tools/flash_usb_msc.ps1`
- Create: `tools/flash_wifi_setup.ps1`
- Create: `tools/flash_all_layout.ps1`
- Modify: `tools/tests/test_app_startup_contracts.py`

- [ ] **Step 1: 写失败脚本契约**

断言单 App offset 分别为 Launcher `0x20000`、Reader `0x220000`、Photo `0x420000`、USB `0x620000`、WiFi `0x820000`。断言首次全布局脚本构建五个实际 App，并在一次 `esptool write_flash` 中写入 bootloader `0x0`、partition table `0x8000`、otadata `0xF000` 和五个 App；不得写 future slot。

- [ ] **Step 2: 验证 RED**

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k flash_layout -q`

Expected: 旧 Reader/Photo offset 和缺失脚本导致失败。

- [ ] **Step 3: 实现可复现脚本**

所有脚本复用 `tools/idf_env.ps1`，使用 `C:\esp\v5.5.4\esp-idf` 和 `C:\Espressif`。全布局脚本先逐个构建，再以 Launcher build 产出的 bootloader/partition table/otadata 以及各 App `.bin` 执行一次 COM 写入。

- [ ] **Step 4: 验证脚本契约**

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k flash_layout -q`

Expected: PASS。

### Task 6: 里程碑 1 审查、构建和实机切换

**Files:**
- No new production files beyond Tasks 1-5.

- [ ] **Step 1: 主代理一次代码审查**

检查分区连续性、所有脚本 offset、Launcher 页面状态边界、局刷区域、Loading 时序、两个最小 App 的 Back 路径，以及禁止依赖是否未引入；发现问题交回同一实现代理修复。

- [ ] **Step 2: 只构建受影响 App**

Run: `idf.py -C apps/launcher build`

Run: `idf.py -C apps/usb_msc build`

Run: `idf.py -C apps/wifi_setup build`

由于 Reader/Photo 分区大小与 offset 已变化，还需确认现有镜像均小于 2MB；首次全布局脚本会复用或构建它们，但本里程碑不运行通用 `tools/build_all.ps1`。

Expected: 三个受影响 App 构建成功；五张镜像均不超过 2MB。

- [ ] **Step 3: COM9 首次全布局烧录**

Run: `tools/flash_all_layout.ps1 -Port COM9`

Expected: 一次写入成功，不启动 monitor、不占用 COM9。

- [ ] **Step 4: 人工切换确认后提交**

验证 MAIN→SETTINGS→Back、SETTINGS→USB→Back→Launcher、SETTINGS→WiFi→Back→Launcher。确认后中文提交里程碑 1。

---

## 里程碑 2：USB MSC

### Task 7: Flash 固化 USB active 图片

**Files:**
- Create: `components/ink_epd_ui/ink_usb_msc_asset.c`
- Modify: `components/ink_epd_ui/include/ink_epd_ui.h`
- Modify: `components/ink_epd_ui/CMakeLists.txt`
- Modify: `components/ink_epd_ui/test/loading_ui_host_test.c`

- [ ] **Step 1: 写失败像素测试**

从 `素材/U盘模式已启动.bmp` 的 200x40、1bpp 像素指纹断言图片极性、固定居中区域和 8px 白边。

- [ ] **Step 2: 验证 RED**

Run: `components/ink_epd_ui/test/run_host_tests.ps1`

Expected: USB 图片 API 不存在。

- [ ] **Step 3: 实现资源与绘制 API**

机械转换 BMP 为 Flash 常量；公开 `ink_epd_ui_usb_msc_active_region()` 与 `ink_epd_ui_draw_usb_msc_active()`，运行时不得读文件或 SD。

- [ ] **Step 4: 验证 GREEN**

Run: `components/ink_epd_ui/test/run_host_tests.ps1`

Expected: PASS。

### Task 8: 独立 USB MSC core

**Files:**
- Create: `components/ink_usb_msc_core/CMakeLists.txt`
- Create: `components/ink_usb_msc_core/include/ink_usb_msc_core.h`
- Create: `components/ink_usb_msc_core/ink_usb_msc_core.c`
- Create: `components/ink_usb_msc_core/test/usb_msc_state_host_test.c`
- Create: `components/ink_usb_msc_core/test/run_host_tests.ps1`

- [ ] **Step 1: 写失败状态测试**

覆盖 `STARTING -> WAITING -> ACTIVE -> STOPPING -> STOPPED`、启动错误进入 ERROR、停止失败保持 ERROR 且不允许返回 Launcher；状态文本不依赖 EPD。

- [ ] **Step 2: 验证 RED**

Run: `components/ink_usb_msc_core/test/run_host_tests.ps1`

Expected: core API 不存在。

- [ ] **Step 3: 迁移最小 raw SDMMC/TinyUSB 逻辑**

参考旧 `main/ink_usb_msc_service.c`，仅保留 SDMMC card、TinyUSB device、`tinyusb_msc_new_storage_sdmmc()`、事件处理、storage 删除和关闭顺序。不得包含 `ink_system_services`、runtime、resource/MSC coordinator；USB 导出期间不得挂载 FATFS。

- [ ] **Step 4: 验证 GREEN**

Run: `components/ink_usb_msc_core/test/run_host_tests.ps1`

Expected: 状态测试 PASS。

### Task 9: USB App 接入、构建与实机验证

**Files:**
- Modify: `apps/usb_msc/main/CMakeLists.txt`
- Modify: `apps/usb_msc/main/app_main.c`
- Modify: `apps/usb_msc/sdkconfig.defaults`
- Modify: `tools/tests/test_app_startup_contracts.py`

- [ ] **Step 1: 写失败集成契约**

断言 App 启动 core，只有 ACTIVE 才画成功图片；Back 先 stop，成功后显示 Loading 并返回，失败留在 USB App。断言无 FATFS mount、无协调器、无 WiFi。

- [ ] **Step 2: 验证 RED**

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k usb_msc -q`

Expected: 最小 App 尚未接入 core。

- [ ] **Step 3: 实现薄 App 胶水**

接入 core 状态和 EPD 页面；日志包含 `APP_START name=usb_msc` 与返回 `BOOT_SWITCH`，不打印扇区内容。停止失败时显示明确错误并继续处理输入。

- [ ] **Step 4: 相关验证和构建**

Run: `components/ink_usb_msc_core/test/run_host_tests.ps1`

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k usb_msc -q`

Run: `idf.py -C apps/usb_msc build`

Expected: PASS，镜像小于 2MB。

- [ ] **Step 5: COM9 单 App 烧录验证**

Run: `tools/flash_usb_msc.ps1 -Port COM9`

人工确认电脑识别 SD、可读写、active 图片正确，安全弹出后 Back 能停止并返回 Launcher；然后中文提交里程碑 2。

---

## 里程碑 3：WiFi 扫描、输入、连接与保存

### Task 10: 单活动 WiFi NVS 契约

**Files:**
- Create: `components/ink_wifi_config/CMakeLists.txt`
- Create: `components/ink_wifi_config/include/ink_wifi_config.h`
- Create: `components/ink_wifi_config/ink_wifi_config.c`
- Create: `components/ink_wifi_config/test/wifi_config_host_test.c`
- Create: `components/ink_wifi_config/test/run_host_tests.ps1`

- [ ] **Step 1: 写失败测试**

覆盖 32-byte SSID、64-byte password 边界；非法长度拒绝；读取失败清零输出；save 写入 `schema_version/ssid/password` 并 commit；连接失败路径不调用 save。测试日志与断言不得包含密码。

- [ ] **Step 2: 验证 RED**

Run: `components/ink_wifi_config/test/run_host_tests.ps1`

Expected: API 不存在。

- [ ] **Step 3: 实现固定公开 API**

实现规格中批准的 `load_active/save_active/clear_active`，内部使用 `nvs_flash_init_partition("wifi_nvs")` 和 `nvs_open_from_partition("wifi_nvs", "ink_wifi", ...)`；保持公开签名稳定。

- [ ] **Step 4: 验证 GREEN**

Run: `components/ink_wifi_config/test/run_host_tests.ps1`

Expected: PASS。

### Task 11: WiFi Setup 纯状态机和键盘

**Files:**
- Create: `components/ink_wifi_setup_core/CMakeLists.txt`
- Create: `components/ink_wifi_setup_core/include/ink_wifi_setup_core.h`
- Create: `components/ink_wifi_setup_core/ink_wifi_setup_core.c`
- Create: `components/ink_wifi_setup_core/test/wifi_setup_core_host_test.c`
- Create: `components/ink_wifi_setup_core/test/run_host_tests.ps1`

- [ ] **Step 1: 写失败状态机测试**

覆盖 AP RSSI 排序、SSID 去重、选择循环、开放网络直连、密码网进入键盘、row-major 前后循环、字符/space/delete/clear/layer/connect、Back 页面转换、连接成功/失败状态与输入保留。

- [ ] **Step 2: 验证 RED**

Run: `components/ink_wifi_setup_core/test/run_host_tests.ps1`

Expected: 状态机不存在。

- [ ] **Step 3: 迁移纯模型**

从旧 `ink_wifi_setup_state/input` 仅拆出纯数据与 reducer；当前 LEFT/RIGHT 物理键映射为语义 up/down，按 row-major 在线性 key index 上移动。不得迁移 MPU/tilt、runtime 或 coordinator。

- [ ] **Step 4: 验证 GREEN**

Run: `components/ink_wifi_setup_core/test/run_host_tests.ps1`

Expected: PASS。

### Task 12: WiFi 异步扫描和连接 core

**Files:**
- Create: `components/ink_wifi_core/CMakeLists.txt`
- Create: `components/ink_wifi_core/include/ink_wifi_core.h`
- Create: `components/ink_wifi_core/ink_wifi_core.c`

- [ ] **Step 1: 定义小型事件 API**

公开 init/start_scan/connect/disconnect/deinit 和事件回调；连接超时由 App 控制为 15 秒，core 只转发 scan done、disconnected、got IP。SSID 可记录，任何日志不得打印 password 或长度。

- [ ] **Step 2: 实现 ESP-IDF 事件桥接**

初始化 `esp_netif`、默认 event loop 和 STA；异步 `esp_wifi_scan_start(..., false)`；连接使用拷贝后的 `wifi_config_t`；deinit 按相反顺序注销 handler 和释放资源。

- [ ] **Step 3: 构建组件消费者**

先在 WiFi App CMake 中临时链接但不启动，运行 `idf.py -C apps/wifi_setup build`。

Expected: 编译链接成功，镜像仍小于 2MB。

### Task 13: WiFi Setup UI

**Files:**
- Create: `components/ink_wifi_setup_ui/CMakeLists.txt`
- Create: `components/ink_wifi_setup_ui/include/ink_wifi_setup_ui.h`
- Create: `components/ink_wifi_setup_ui/ink_wifi_setup_ui.c`
- Create: `components/ink_wifi_setup_ui/test/wifi_setup_ui_host_test.c`
- Create: `components/ink_wifi_setup_ui/test/run_host_tests.ps1`

- [ ] **Step 1: 写失败像素/区域测试**

覆盖 SCANNING、AP_LIST、PASSWORD_KEYBOARD、CONNECTING、SUCCESS、ERROR 页面；AP/键盘选择区域局刷必须覆盖旧新 marker，页面切换区域不得越过 480x800。

- [ ] **Step 2: 验证 RED**

Run: `components/ink_wifi_setup_ui/test/run_host_tests.ps1`

Expected: renderer 不存在。

- [ ] **Step 3: 实现 bounded renderer**

迁移旧 UI 的键盘布局观感，但只依赖 framebuffer primitives 和 Flash 内置 UI 字；不得读取 SD 字体。密码只显示掩码，不缓存到日志字符串。

- [ ] **Step 4: 验证 GREEN**

Run: `components/ink_wifi_setup_ui/test/run_host_tests.ps1`

Expected: PASS。

### Task 14: WiFi App 集成和 NVS 保存

**Files:**
- Modify: `apps/wifi_setup/main/CMakeLists.txt`
- Modify: `apps/wifi_setup/main/app_main.c`
- Modify: `apps/wifi_setup/sdkconfig.defaults`
- Modify: `tools/tests/test_app_startup_contracts.py`

- [ ] **Step 1: 写失败集成契约**

断言先显示 SCANNING 再异步扫描；获取 IP 后才 `ink_wifi_config_save_active()`；disconnect/15 秒超时不保存；AP_LIST Back 和成功页 Back 均显示 Loading 返回 Launcher；禁止 password 日志。

- [ ] **Step 2: 验证 RED**

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k wifi_setup -q`

Expected: 最小 App 未集成。

- [ ] **Step 3: 实现薄事件循环**

把 core 事件转成纯状态机动作；列表/键盘 marker 用区域局刷，扫描结果和页面变化用全刷。成功保存单活动配置并显示 `WiFi 已保存`；失败保留输入，旧配置不变。

- [ ] **Step 4: 相关验证和构建**

Run: `components/ink_wifi_config/test/run_host_tests.ps1`

Run: `components/ink_wifi_setup_core/test/run_host_tests.ps1`

Run: `components/ink_wifi_setup_ui/test/run_host_tests.ps1`

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k wifi_setup -q`

Run: `idf.py -C apps/wifi_setup build`

Expected: PASS，镜像小于 2MB。

### Task 15: 里程碑完整验证、COM9 和提交

**Files:**
- Modify: `tools/build_all.ps1`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/MIGRATION_NOTES.md`

- [ ] **Step 1: 更新完整构建清单与架构记录**

`build_all.ps1` 构建 Launcher/Reader/Photo/USB/WiFi；文档记录固定分区 ABI、独立 App 所有权、已迁移的纯 USB/WiFi 逻辑和明确未迁移的 runtime/coordinator/MPU。

- [ ] **Step 2: 完整 host + pytest**

运行所有组件和 Reader host 脚本，再运行 `python -m pytest tools/tests -q`。

Expected: 全部 PASS。

- [ ] **Step 3: 禁止依赖扫描**

Run: `rg -n -i "ink_system_runtime|ink_system_services|resource_coordinator|usb_msc_coordinator|wifi_coordinator|voice_note|asr|i2s|usb msc service|mpu|tilt" apps components`

Expected: 新 USB/WiFi production source 无禁止依赖；允许命中的测试断言需人工确认仅用于禁止检查。

- [ ] **Step 4: 最终 build_all**

Run: `tools/build_all.ps1`

Expected: 五个 App 全部成功且每个镜像小于 2MB。

- [ ] **Step 5: COM9 WiFi 实机验证**

Run: `tools/flash_wifi_setup.ps1 -Port COM9`

人工确认扫描、上下选择、完整键盘遍历、连接成功保存、失败不覆盖、重启可读；用户自行抓日志，任务不启动 monitor。

- [ ] **Step 6: 一次最终代码审查并中文提交**

检查资源所有权、停止/反初始化顺序、凭据边界和日志泄漏、局刷范围、五 App 分区大小；修复后重复受影响验证，最后中文提交里程碑 3。
