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

## Task 7：构建与烧录工具

新增 `tools/idf_env.ps1`，固定 `IDF_PATH=C:\esp\v5.5.4\esp-idf` 和 `IDF_TOOLS_PATH=C:\Espressif`。`build_all.ps1` 顺序构建 launcher、reader、photo，任一失败立即退出。

烧录脚本都要求显式 `-Port`：launcher 写 bootloader、partition table、otadata 与 `0x20000` factory image；reader 只写 `0x120000`；photo 只写 `0x520000`。未连接实机时不执行烧录。

## Task 8：最终验证

在 ESP-IDF 5.5.4 环境下删除三个 build 目录后执行 `tools/build_all.ps1`，随后执行一次无改动的顺序构建确认完整退出状态。最终结果：

| app | 镜像大小 | 分区大小 | build |
| --- | ---: | ---: | --- |
| launcher | 230,992 bytes | 1,048,576 bytes | PASS |
| reader | 337,072 bytes | 4,194,304 bytes | PASS |
| photo | 336,352 bytes | 4,194,304 bytes | PASS |

最终 `build_all.ps1` 退出码为 0，三个子项目均输出 `Project build complete`。PowerShell 脚本 AST 解析通过，`git diff --check` 通过。

源码排除项扫描未发现 voice note、ASR、I2S、WiFi、TinyUSB/USB MSC、resource coordinator、background flush、runtime shell、display mailbox 或 aggressive interrupt。构建组件闭包不包含 `esp_wifi` 或 `esp_driver_i2s`；FAT/VFS 的通用串口 console 依赖不是 USB MSC 服务。

三个 `APP_START` 和四个 `BOOT_SWITCH` 精确日志字符串均已静态核验。

### 待上板验证

当前未提供串口和连接设备，因此以下项目没有宣称通过：GDEY0426T82 实际刷新效果与 BUSY 时序、SD 卡实际挂载和文件读取、按键电平/长按、三个分区间的真实重启切换，以及运行日志中不存在 panic/内存/面板超时错误。应使用 `flash_launcher.ps1 -Port <PORT>` 首次写入完整布局，再分别写 reader/photo 镜像并执行验收流程。

## Task 9：COM9 首次上板验证

首次执行烧录脚本时发现 esptool 4.12 拒绝连字符参数，脚本在 build 后以退出码 2 结束，Flash 实际未写入。已将三个脚本统一修正为 `default_reset`、`hard_reset`、`write_flash`、`flash_mode`、`flash_size` 和 `flash_freq`。随后在 COM9 完成写入并逐段看到 `Hash of data verified`：launcher 完整布局、reader `0x120000`、photo `0x520000` 均退出码为 0。

launcher 首次启动在 `APP_START name=launcher` 后发生 `TG1WDT_SYS_RST`。根因是 `ink_epd_ui_self_test()` 在 3584 字节主任务栈上声明了 48,000 字节 framebuffer，造成栈破坏和 DoubleException。缓冲区改为堆分配后，launcher 可稳定运行超过 15 秒，未再出现 WDT、panic、assert、Guru Meditation、EPD timeout 或禁止模块日志。

已观察到 reader 从 `0x120000` 启动并打印 `APP_START name=reader`，SD 成功挂载到 `/sdcard`；设备未找到字体时按设计降级到内置状态字体。reader 在观察窗口内保持稳定。将 otadata 恢复为空白后，bootloader 正确回退 factory 分区并打印 `APP_START name=launcher`。

修复后再次执行 `tools/build_all.ps1`，launcher 231,008 bytes、reader 337,072 bytes、photo 336,352 bytes，三个项目均构建通过，脚本退出码为 0。

### 尚待实体操作确认

- launcher 屏幕实际显示 Reader / Photo。
- Confirm 执行 launcher -> reader，reader Back 长按返回 launcher。
- 选择 Photo 后执行 launcher -> photo，左右切图，photo Back 长按返回 launcher。
- photo 的实际启动日志、SD 图片加载和屏幕刷新效果。

在上述实体交互完成前，不宣称第一阶段实机验收全部通过，也不合并到 master。

## Task 10：旧版 UI、SD 字体和照片全刷对齐

本轮继续保持三个独立固件和共享小组件边界，没有恢复旧 runtime、display mailbox、后台协调器或多 app 同进程调度。

### 字体与 UTF-8

- `ink_fonts` 从旧版提取同步 cpfont v4 加载、interval/glyph 查找、2bit 字形解码、小型缓存、宽度测量和 480x800 单色 framebuffer 绘制。
- 删除旧 `read_at`/SDIO service 接点，不依赖 EPD 驱动、runtime 或 resource coordinator。
- loader 增加文件长度、offset 加乘法溢出、interval 顺序/glyph 范围和 bitmap 长度校验；损坏字体在绘制前失败。
- menu/footer/reader 候选路径沿用旧版，固定路径失败后扫描 `/sdcard/fonts`、`/sdcard/FONTS` 和 `/sdcard/.fonts` 下一层 family 目录。
- UTF-8 截断按 codepoint 处理，非法序列替换为 `U+FFFD`，不再按字节把中文显示成连续问号。

### launcher

- launcher 允许挂载 SD，但只加载 menu/footer 字体，不扫描书籍或图片。
- 页面恢复旧 crosspoint 几何：header gutter 24、divider y=38、list x=24、row width=432、row height=70、gap=6。
- 只使用旧列表前两行，显示书本/相册图标、说明文字和右侧 chevron；选中状态保持用户指定的文字左侧横线，不使用黑底反白。
- 首次进入全刷，选择变化仍只刷新旧、新横线联合区域。

### reader

- 按顺序扫描 `/sdcard/books`，再扫描 `/sdcard` 根目录中的 `.xtc/.xtch`；目录内不区分大小写排序。
- 坏候选不会阻塞后续有效书，扫描结果区分 SD 失败、目录缺失、空目录、格式错误、I/O 错误和成功打开。
- 状态页使用 cpfont，书页继续直接显示 XTG/XTH 预渲染 framebuffer；Back 仍为短按返回 launcher。

### photo 与灰阶全刷

- 相册列表加载 menu/footer cpfont，标题和中文文件名按 codepoint 截断、按字体实际宽度测量；无字体时只显示英文 fallback，不输出乱码。
- 列表仍采用局刷和累计 50 次后下一次全刷策略。
- 图片预览和预览左右切图只调用 `ink_hw_gray_refresh()`，调用前打印 `PHOTO_REFRESH mode=full_gray`。
- 灰阶初始化补回旧版 `0x18 -> 0x80`，恢复旧版 GDEY0426T82 灰阶 LUT；完整窗口仍为 800x480 像素坐标，随后依次写 `0x26` MSB、`0x24` LSB 和 `0x22 -> 0xC7` 更新。

### 自动验证

在 `IDF_PATH=C:\esp\v5.5.4\esp-idf`、`IDF_TOOLS_PATH=C:\Espressif` 下执行 `tools/build_all.ps1`，退出码为 0：

| app | 镜像大小 | 分区大小 | build |
| --- | ---: | ---: | --- |
| launcher | 348,384 bytes | 1,048,576 bytes | PASS |
| reader | 347,280 bytes | 4,194,304 bytes | PASS |
| photo | 352,960 bytes | 4,194,304 bytes | PASS |

源码排除项扫描未发现 voice note、ASR、I2S、WiFi、TinyUSB/USB MSC、resource coordinator、background flush、runtime shell、display mailbox 或 aggressive interrupt。`git diff --check` 通过。

objdump 静态栈帧检查：launcher `app_main=128B`、reader `app_main=768B`、photo `app_main=128B`；`ink_cpfont_self_test=480B`、`ink_fonts_self_test=416B`、`ink_epd_ui_self_test=208B`、`ink_reader_core_self_test=448B`、`ink_hw_self_test=32B`。未在 3584B 主任务栈上放置 framebuffer 或字体 bitmap 大数组。

### 待 COM9 验收

本节记录时尚未烧录本轮镜像，因此不宣称以下项目通过：cpfont 实际读取与中文显示、launcher 新两卡布局、reader 书籍分类结果、photo 旧灰阶 LUT 全刷效果和完整三 app 按键往返。下一步先在 COM9 烧录三个镜像，再按串口和屏幕实际观察结果更新本节。

## Task 11：COM9 旧版 UI、字体与灰阶首轮验收

三个镜像均通过 COM9 写入并出现 `Hash of data verified`：launcher 完整布局、reader `0x120000`、photo `0x520000`。实机日志确认 launcher、reader、photo 分别打印对应的 `APP_START`，SD 挂载到 `/sdcard`。

首轮固件完成了以下实际交互：

- launcher 通过 Confirm 切换到 reader；reader 打开 `/sdcard/books` 下的 XTC 书籍并读取 `2658` 页。
- reader 短按 Back 打印 `BOOT_SWITCH from=reader to=launcher` 并返回 launcher。
- launcher 切换到 photo；`READ.BMP` 打印 `PHOTO_REFRESH mode=full_gray` 并完成显示。
- 另一张中文文件名 BMP 在解码阶段失败，photo 保持原索引，没有 panic 或状态错乱。

首轮日志同时证明 SD 卡上当时没有可用 cpfont，launcher/photo 按设计降级为 ASCII。观察窗口未出现 panic、assert、Guru Meditation、SD 内存不足、EPD timeout 或禁止模块日志。

## Task 12：修复中文 FatFS 路径与 BMP 入库探测

### 根因与修复

旧版在 BMP 加入相册前先执行格式探测，新版最初只按 `.bmp` 扩展名入库，导致不支持文件到打开阶段才报错。现已恢复入库前探测，只接受 `480x800`、4bpp indexed、BI_RGB、bottom-up、4 到 16 色且调色板完整的 BMP；失败日志包含 path 和 reason。探测与 decoder 共用同一 metadata 校验，覆盖 palette offset、短 palette 和 32 位 `fseek` 上界。

更关键的迁移回归是三个 v2 app 默认使用 `CONFIG_FATFS_LFN_NONE` 和 CP437。实机 reader 日志把中文书名输出为乱码，中文文件路径重新 `fopen()` 也可能失败。launcher、reader、photo 的 `sdkconfig.defaults` 现统一启用：

```text
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_CODEPAGE_936=y
CONFIG_FATFS_API_ENCODING_UTF_8=y
```

删除三个生成的 `sdkconfig` 和 build 目录后重新构建，实际生成配置均为 LFN heap、CP936、`CODEPAGE=936` 和 UTF-8，LFN_NONE/CP437 均为 not set。

### 构建与烧录

`tools/build_all.ps1` 退出码为 0：

| app | 镜像大小 | 分区大小 | build |
| --- | ---: | ---: | --- |
| launcher | 526,800 bytes | 1,048,576 bytes | PASS |
| reader | 525,712 bytes | 4,194,304 bytes | PASS |
| photo | 532,688 bytes | 4,194,304 bytes | PASS |

三镜像随后重新写入 COM9，launcher 完整布局、reader `0x120000`、photo `0x520000` 均出现 `Hash of data verified`，烧录脚本退出码均为 0。

最终再次执行无源码改动的 `tools/build_all.ps1`，三个 app 均输出 `Project build complete`，脚本退出码为 0。禁止模块扫描为 0 命中，`git diff --check` 通过。objdump 栈帧为 launcher `app_main=128B`、reader `app_main=768B`、photo `app_main=128B`、`ink_photo_core_self_test=368B`，均远低于 3584B 主任务栈。

### 新固件实机证据

- launcher 打印 `APP_START name=launcher`，SD 挂载成功，并加载 `/sdcard/fonts/LXGWWenKai_24.cpfont` 与 `/sdcard/fonts/SmallSimSunEmbedded_16.cpfont`。
- 通过 otadata 直接选择 reader 后，reader 打印 `APP_START name=reader`，正确输出 UTF-8 路径 `/sdcard/books/作家榜经典：磨坊信札.xtc`，打开 `1071` 页；书名不再乱码。
- 通过 otadata 直接选择 photo 后，photo 打印 `APP_START name=photo`，SD、menu font 和 footer font 均加载成功；启动期间没有 `catalog skip ... open_failed`。
- 上述启动观察均未出现 panic、assert、Guru Meditation、SD 内存不足、panel reset timeout、busy wait aborted、WiFi、voice note、I2S、ASR 或 USB MSC 日志。

### 仍待实体确认

本轮新固件尚未收到 photo Confirm、左右切图或 Back 按键事件，因此不把以下项目写成 PASS：中文 BMP 实际打开后的灰阶全刷、预览左右切图，以及 reader/photo 短按 Back 返回 launcher。launcher 卡片尺寸、图标、chevron、左侧横线和屏幕灰阶效果也需要以实体屏幕观察确认。旧固件首轮已验证对应切换路径，但最终验收仍以本轮镜像为准。

## Task 13：最终审查修复与回归测试

最终全量审查发现并修复以下状态一致性问题：

- reader 扫描候选时不仅验证 XTC/XTCH header 和 page index，还预验证首屏 XTG/XTH payload 完整性；首个候选首屏损坏时继续尝试后续书籍。
- reader 新增事务式目标页加载：XTG 和 XTH 都先完整读入 PSRAM/heap 临时缓冲，成功后才更新 framebuffer 与 `current_page`；短读、分配或格式失败保持原页码和原 framebuffer。
- photo 对 4 到 16 项 palette 的每个 index 按亮度量化到四灰阶，不再把中间 palette index 全部映射为白色；四项等间隔 palette 仍保持黑、深灰、浅灰、白编码。
- launcher 选择变化只在局刷或 fallback 全刷成功后提交；两种刷新均失败时，Confirm 仍指向屏幕可见的旧选择，并恢复 framebuffer 中的旧横线状态。

新增两个不进入固件镜像的 host test runner：

```powershell
components/ink_reader_core/test/run_host_tests.ps1
components/ink_photo_core/test/run_host_tests.ps1
```

reader 测试使用每次 GUID 隔离的 TEMP fixture/build/tool root，覆盖坏 XTG/XTH 首屏候选回退、XTG/XTH 目标页截断及页码/framebuffer 事务性；photo 测试覆盖 16 色 palette 到四灰阶的完整映射。两个 runner 最终均输出 PASS，未创建或修改实体 `/sdcard` 文件。

最终执行 `tools/build_all.ps1`，退出码为 0：

| app | 镜像大小 | 分区大小 | build |
| --- | ---: | ---: | --- |
| launcher | 526,896 bytes | 1,048,576 bytes | PASS |
| reader | 526,224 bytes | 4,194,304 bytes | PASS |
| photo | 532,672 bytes | 4,194,304 bytes | PASS |

禁止模块源码扫描为 0 命中；objdump 栈帧保持 launcher `app_main=128B`、reader `app_main=768B`、photo `app_main=128B`。最终三镜像再次写入 COM9，launcher 完整布局、reader `0x120000`、photo `0x520000` 均出现 `Hash of data verified`。

本轮最终镜像仍需要实体按键完成 photo Confirm/左右/Back 和 reader Back，并观察 launcher 卡片、横线局刷及 photo 四灰阶全刷效果；在这些屏幕/按键项目完成前仍不合并 master。
