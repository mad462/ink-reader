# 迁移记录

## Task 1：工作区骨架与统一配置

已创建 launcher、reader、photo 三个独立 ESP-IDF 子项目。三个项目共享根目录 `components`，使用 ESP32-S3、16MB Flash、8MB Octal PSRAM 的统一默认配置，并引用同一张 16MB 自定义分区表。

### 分区验证

验证环境：

- `IDF_PATH=C:\esp\v5.5.4\esp-idf`
- `IDF_TOOLS_PATH=C:\Espressif`
- Python：`C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe`

执行命令：

```powershell
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
$env:IDF_TOOLS_PATH='C:\Espressif'
& 'C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe' `
  "$env:IDF_PATH\components\partition_table\gen_esp32part.py" `
  --flash-size 16MB `
  partitions\partitions_16mb.csv `
  (Join-Path $env:TEMP 'ink-reader-v2-partitions.bin')
```

结果：命令退出码为 0，输出 3072 字节的二进制分区表；app 分区均按 `0x10000` 对齐，最大分区末端为 `0x1000000`，等于 16MB Flash 上限且未越界。将二进制表反解析后得到 `launcher=factory@0x20000/1M`、`reader=ota_0@0x120000/4M`、`photo=ota_1@0x520000/4M` 和 `data=fat@0x920000/7040K`。

### 当前限制

三个 `app_main.c` 尚未创建，本任务不要求应用构建成功。硬件初始化、EPD 显示、按键、SD 和启动分区切换均留待后续任务实现与上板验证。

## Task 2：基础 UI、输入与 launcher 最小显示

### RED

launcher 先引用尚不存在的 `ink_epd_ui.h`。修正骨架中缺失的 `components` 目录后，构建在 `app_main.c:1` 以 `fatal error: ink_epd_ui.h: No such file or directory` 失败，证明新接口被实际编译。

构建同时发现普通 CMake 变量不能启用 ESP-IDF 5.5.4 minimal build，已统一改用 `idf_build_set_property(MINIMAL_BUILD ON)`。这使三个项目只构建 main 及传递依赖，不把 WiFi/lwIP 等未使用组件纳入固件。

### GREEN

新增：

- `ink_epd_ui`：480x800 单色 framebuffer、像素/矩形、5x7 ASCII 和 launcher/status 页面。
- `ink_input`：GPIO9/10/12/11/46 低有效轮询、20ms 去抖、按下/释放/持续时间快照。
- `ink_hw`：GPIO4/5/6/7/15/16 的 GDEY0426T82 同步 SPI 驱动，仅保留初始化、单色全刷、四灰阶全刷和休眠。

没有迁移旧驱动的 cancel callback、phase、aborted error、partial interrupt、mailbox 或 runtime 逻辑。launcher 启动时打印 `APP_START name=launcher`，显示 Reader/Photo 并支持 Left/Right 选择。

launcher 构建生成 `build/launcher.bin`，首次结果大小为 218,912 字节。硬件显示效果和 BUSY 时序仍需上板验证。

## Task 3：boot switch

### RED

launcher Confirm 路径先引用 `ink_boot_switch.h`，构建按预期以 `fatal error: ink_boot_switch.h: No such file or directory` 失败。

### GREEN

新增 `ink_boot_switch`，按 `launcher`、`reader`、`photo` label 查找 app partition，成功执行 `esp_ota_set_boot_partition()` 后调用 `esp_restart()`。目标缺失或设置失败时返回错误且不重启。

launcher 在切换前打印精确的 `BOOT_SWITCH from=launcher to=reader` 或 `BOOT_SWITCH from=launcher to=photo`。

## Task 4：SD 组件与 reader 最小启动

### RED

reader 先依赖尚不存在的 `ink_sd`，CMake 按预期以 `Failed to resolve component 'ink_sd'` 失败。

### GREEN

新增 `ink_sd`，固定以 SDMMC 4-bit 挂载 `/sdcard`：CLK40、CMD39、D0 41、D1 42、D2 48、D3 38。禁止自动格式化，mount 失败只返回错误。

reader 打印 `APP_START name=reader`，初始化 EPD、按键并挂载 SD。当前最小状态显示 `NO BOOKS FOUND` 或 `SD CARD ERROR`。Back 持续 1200ms 后只触发一次 `BOOT_SWITCH from=reader to=launcher`。

## Task 5：reader 核心与字体

### RED

reader 先声明尚不存在的 `ink_fonts` 和 `ink_reader_core`，构建按预期以 `Failed to resolve component 'ink_fonts'` 失败。

### GREEN

新增 `ink_reader_core`，只迁移 XTC/XTCH 的纯业务能力：header/page index 校验、`/sdcard/books` 与根目录扫描、打开/关闭、XTG/XTH 页读取以及页边界导航。没有迁移 session、app state、file browser 或 runtime。

新增 `ink_fonts`，从兼容路径读取 cpfont header，验证 `CPFONT` magic、v4 版本和 style count。字体缺失时 reader 使用内置 ASCII 状态字体继续运行。

reader 找到书后加载第一页，Left/Right 在有效边界内翻页；没有书籍时显示 `NO BOOKS FOUND`。

## Task 6：photo 与 BMP 核心

### RED

photo 先声明尚不存在的 `ink_photo_core`，构建按预期以 `Failed to resolve component 'ink_photo_core'` 失败。

### GREEN

新增 `ink_photo_core`：非递归扫描 `/sdcard/photos`，只接受大小写不敏感 `.bmp` 并排序；BMP parser 只接受 480x800、BI_RGB、4bpp indexed bottom-up 文件，并按 palette luminance 输出两个四灰阶平面。

photo 打印 `APP_START name=photo`。有图片时显示第一张，Left/Right 循环切图；空目录显示 `NO PHOTOS FOUND`，解析失败显示 `IMAGE ERROR`。Back 长按打印 `BOOT_SWITCH from=photo to=launcher` 并切回 launcher。
