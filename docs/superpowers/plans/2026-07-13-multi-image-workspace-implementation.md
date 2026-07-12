# ESP32-S3 多固件电子墨水工作区实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标：** 构建 launcher、reader、photo 三个独立 ESP-IDF 5.5.4 固件，共享小型组件并通过 OTA boot partition 本地切换。

**架构：** 三个子项目使用相同的 16MB 分区表和根目录共享组件。每个固件同步独占 EPD、按键和可选 SD，不引入 runtime、mailbox、后台协调器或网络/音频/USB 依赖。

**技术栈：** ESP-IDF 5.5.4、ESP32-S3、C、CMake、PowerShell、SDMMC/FATFS、ESP OTA APIs。

---

## 文件结构

- `partitions/partitions_16mb.csv`：统一分区布局与固定镜像偏移。
- `components/ink_hw/`：同步 GDEY0426T82 驱动。
- `components/ink_input/`：GPIO 按键扫描、去抖和长按快照。
- `components/ink_sd/`：SDMMC 4-bit `/sdcard` 挂载。
- `components/ink_epd_ui/`：framebuffer primitives 与 ASCII 状态页。
- `components/ink_fonts/`：可选 cpfont 读取和绘制。
- `components/ink_reader_core/`：XTC/XTCH 扫描、解析与页读取。
- `components/ink_photo_core/`：BMP 扫描、解析与灰阶转换。
- `components/ink_boot_switch/`：按 label 切换启动分区并重启。
- `apps/*/main/app_main.c`：三个薄固件入口。
- `tools/*.ps1`：统一环境构建和按固定 offset 烧录。
- `docs/ARCHITECTURE.md`：面向维护者的架构说明。
- `docs/MIGRATION_NOTES.md`：逐步构建和迁移记录。

## Task 1：工作区骨架与统一配置

**文件：**
- 创建：`.gitignore`
- 创建：`partitions/partitions_16mb.csv`
- 创建：`apps/launcher/CMakeLists.txt`
- 创建：`apps/launcher/sdkconfig.defaults`
- 创建：`apps/launcher/main/CMakeLists.txt`
- 创建：`apps/reader/CMakeLists.txt`
- 创建：`apps/reader/sdkconfig.defaults`
- 创建：`apps/reader/main/CMakeLists.txt`
- 创建：`apps/photo/CMakeLists.txt`
- 创建：`apps/photo/sdkconfig.defaults`
- 创建：`apps/photo/main/CMakeLists.txt`
- 创建：`docs/ARCHITECTURE.md`
- 创建：`docs/MIGRATION_NOTES.md`

- [ ] 创建目录和三个只引用根 `components` 的 ESP-IDF 项目。
- [ ] 配置 `esp32s3`、16MB Flash、8MB Octal PSRAM 和统一自定义分区表。
- [ ] 用 `gen_esp32part.py partitions/partitions_16mb.csv` 验证分区表合法且不超过 16MB。
- [ ] 在迁移记录中写入骨架与分区验证结果。
- [ ] 提交：`工程：创建多固件工作区骨架`。

## Task 2：基础 UI、输入与 launcher 最小显示

**文件：**
- 创建：`components/ink_epd_ui/CMakeLists.txt`
- 创建：`components/ink_epd_ui/include/ink_epd_ui.h`
- 创建：`components/ink_epd_ui/ink_epd_ui.c`
- 创建：`components/ink_input/CMakeLists.txt`
- 创建：`components/ink_input/include/ink_input.h`
- 创建：`components/ink_input/ink_input.c`
- 创建：`components/ink_hw/CMakeLists.txt`
- 创建：`components/ink_hw/include/ink_hw.h`
- 创建：`components/ink_hw/ink_hw.c`
- 创建：`apps/launcher/main/app_main.c`

- [ ] 先让 launcher 引用尚不存在的 `ink_epd_ui_draw_launcher()`，运行 build 并确认因接口缺失失败。
- [ ] 实现 480x800 单色 framebuffer、像素、矩形和 5x7 ASCII 文本；self-test 验证像素边界和 launcher 页面确实产生黑像素。
- [ ] 从旧按键代码迁移 GPIO9/10/12/11/46 的轮询与 20ms 去抖，公开 `ink_input_poll()`；self-test 验证按下、释放和 held_ms。
- [ ] 从旧 EPD 驱动提取同步初始化和单色全刷路径，删除 cancel callback、phase、aborted error 与 partial interrupt 接口。
- [ ] launcher 打印 `APP_START name=launcher`，初始化硬件并显示 Reader/Photo，Left/Right 改变选择。
- [ ] 运行 launcher build，必须生成 `launcher.bin`。
- [ ] 更新迁移记录并提交：`功能：实现最小启动器显示`。

## Task 3：boot switch

**文件：**
- 创建：`components/ink_boot_switch/CMakeLists.txt`
- 创建：`components/ink_boot_switch/include/ink_boot_switch.h`
- 创建：`components/ink_boot_switch/ink_boot_switch.c`
- 修改：`apps/launcher/main/CMakeLists.txt`
- 修改：`apps/launcher/main/app_main.c`

- [ ] 在 launcher Confirm 路径引用三个尚未实现的切换 API，构建并确认链接失败。
- [ ] 实现 label 常量 `launcher`、`reader`、`photo`，使用 `esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label)`。
- [ ] 目标存在时调用 `esp_ota_set_boot_partition()`；成功后调用 `esp_restart()`，失败时返回错误且不重启。
- [ ] launcher 在调用前打印精确 `BOOT_SWITCH from=launcher to=...` 日志。
- [ ] 重新构建 launcher，更新迁移记录并提交：`功能：添加本地固件启动切换`。

## Task 4：SD 组件与 reader 最小启动

**文件：**
- 创建：`components/ink_sd/CMakeLists.txt`
- 创建：`components/ink_sd/include/ink_sd.h`
- 创建：`components/ink_sd/ink_sd.c`
- 创建：`apps/reader/main/app_main.c`

- [ ] reader 引用尚不存在的 `ink_sd_mount()` 并构建，确认缺失接口失败。
- [ ] 实现 SDMMC 4-bit 挂载：CLK40、CMD39、D0 41、D1 42、D2 48、D3 38，挂载点固定 `/sdcard`，`format_if_mount_failed=false`。
- [ ] reader 打印 `APP_START name=reader`，初始化 EPD/按键，挂载 SD，显示 `NO BOOKS FOUND` 占位状态。
- [ ] Back held_ms >= 1200 只打印一次 `BOOT_SWITCH from=reader to=launcher` 并调用切换组件。
- [ ] 构建 reader，更新迁移记录并提交：`功能：实现阅读器最小启动`。

## Task 5：reader 核心与字体

**文件：**
- 创建：`components/ink_reader_core/CMakeLists.txt`
- 创建：`components/ink_reader_core/include/ink_reader_core.h`
- 创建：`components/ink_reader_core/ink_reader_core.c`
- 创建：`components/ink_fonts/CMakeLists.txt`
- 创建：`components/ink_fonts/include/ink_fonts.h`
- 创建：`components/ink_fonts/ink_fonts.c`
- 修改：`apps/reader/main/CMakeLists.txt`
- 修改：`apps/reader/main/app_main.c`

- [ ] 写 XTC 纯解析 self-test，覆盖错误 magic、页索引范围和 next/previous 边界；先构建确认实现缺失。
- [ ] 从旧 `ink_xtc_reader`/`ink_xtc_book` 提取 header、page index、打开/关闭和 480x800 页位图读取，不迁移 session/app_state/runtime。
- [ ] 扫描 `/sdcard/books`，无结果时兼容扫描 `/sdcard`，只接受 `.xtc/.xtch`。
- [ ] 提供 cpfont 只读 load/close API，reader 按兼容路径尝试加载；字体失败记录 warning 但继续。
- [ ] reader 打开第一本书，显示当前页；Left/Right 在有效边界内翻页。
- [ ] 构建 reader，更新迁移记录并提交：`功能：迁移最小 XTC 阅读链路`。

## Task 6：photo 最小启动与 BMP 核心

**文件：**
- 创建：`components/ink_photo_core/CMakeLists.txt`
- 创建：`components/ink_photo_core/include/ink_photo_core.h`
- 创建：`components/ink_photo_core/ink_photo_core.c`
- 创建：`apps/photo/main/app_main.c`

- [ ] 写 BMP header/palette/pixel packing self-test，并让 photo 引用缺失 parser，构建确认失败。
- [ ] 从旧 catalog/parser 提取 `/sdcard/photos` 非递归扫描、大小写不敏感 `.bmp` 过滤和排序。
- [ ] 支持 480x800、BI_RGB、4bpp indexed BMP，转换到两个 48000-byte 灰阶平面。
- [ ] photo 打印 `APP_START name=photo`；无图片显示 `NO PHOTOS FOUND`，有图片显示第一张，Left/Right 循环切换。
- [ ] Back 长按只打印一次 `BOOT_SWITCH from=photo to=launcher` 并切回 launcher。
- [ ] 构建 photo，更新迁移记录并提交：`功能：实现最小 BMP 相册`。

## Task 7：构建与烧录工具

**文件：**
- 创建：`tools/idf_env.ps1`
- 创建：`tools/build_all.ps1`
- 创建：`tools/flash_launcher.ps1`
- 创建：`tools/flash_reader.ps1`
- 创建：`tools/flash_photo.ps1`

- [ ] 环境脚本固定 `IDF_PATH=C:\esp\v5.5.4\esp-idf` 与 `IDF_TOOLS_PATH=C:\Espressif` 并调用 `export.ps1`。
- [ ] `build_all.ps1` 依次构建三个子项目，任一失败立即退出非零。
- [ ] launcher flash 脚本写 bootloader、partition table、boot_app0 和 launcher app；reader/photo 只按固定 app offset 写各自 bin。
- [ ] 所有脚本要求显式 `-Port`，不猜测串口。
- [ ] 运行 `build_all.ps1`，更新迁移记录并提交：`工具：添加多固件构建与烧录脚本`。

## Task 8：最终验证与排除项审计

**文件：**
- 修改：`docs/MIGRATION_NOTES.md`
- 修改（仅在验证失败时）：对应组件或 app 文件

- [ ] 清理三个 build 目录后执行 `tools/build_all.ps1`，记录三个 exit code 和镜像大小。
- [ ] 用 `rg` 审计源码与构建依赖，不得出现 WiFi、voice_note、ASR、I2S、USB MSC、resource coordinator、background flush、runtime shell、display mailbox。
- [ ] 用 `rg` 审计三个精确 `APP_START` 和四个精确 `BOOT_SWITCH` 日志。
- [ ] 检查每个 `app_main.c` 只包含顺序初始化、显示和输入循环。
- [ ] 检查 Git diff、提交状态和创建文件清单。
- [ ] 将未连接实机而无法证明的 EPD、SD、按键、重启切换和禁用日志验收明确记录为待上板验证。

## 固定验证命令

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
python "$env:IDF_PATH\components\partition_table\gen_esp32part.py" partitions\partitions_16mb.csv
idf.py -C apps\launcher build
idf.py -C apps\reader build
idf.py -C apps\photo build
```

预期三个构建均以 `Project build complete` 结束；分区校验输出总大小不超过 `0x1000000`。
