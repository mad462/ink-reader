# ESP32-S3 多固件电子墨水工作区设计

## 目标

在 `D:\FUCKIDF\ink-reader-v2` 建立一个干净的 ESP-IDF 5.5.4 工作区，为 ESP32-S3、16MB Flash、8MB Octal PSRAM 和 GDEY0426T82 屏幕构建三个相互独立的固件：launcher、reader、photo。

第一阶段只实现本地启动分区切换、XTC 阅读和 BMP 相册。旧项目 `D:\FUCKIDF\ink-reader` 只作为只读参考，不迁移 WiFi、语音、I2S、USB MSC、后台服务、资源协调器、runtime shell、display mailbox 或复杂 app registry。

## 总体架构

工作区包含三个独立 ESP-IDF 子项目：

- `apps/launcher`：显示 Reader 和 Photo 两个入口。
- `apps/reader`：挂载 SD，扫描并阅读 XTC/XTCH 书籍。
- `apps/photo`：挂载 SD，扫描并显示 4bpp BMP 图片。

三个项目通过 `EXTRA_COMPONENT_DIRS` 引用工作区根目录下的共享组件。每次只运行一个固件；不存在多 app 同进程 runtime，也不存在跨 app 常驻任务。

每个 `app_main.c` 只负责顺序初始化、页面呈现和同步输入循环。当前固件独占 EPD、SD 和按键。

## 分区设计

`partitions/partitions_16mb.csv` 使用同一张 16MB 分区表：

- `nvs`
- `otadata`
- `phy_init`
- `launcher`：factory app
- `reader`：OTA slot 0
- `photo`：OTA slot 1
- 剩余空间作为可选 data 分区

三个 app image 均使用相同分区表。构建产物不依赖默认 app-flash 目标；专用 PowerShell 脚本按分区表中的固定偏移写入对应二进制，避免 reader/photo 被误写到 factory 分区。

## 共享组件

### ink_hw

提供 GDEY0426T82 同步驱动和基础 framebuffer 绘制接口。沿用旧项目已验证的 SPI 引脚和面板初始化/波形序列：MOSI GPIO4、SCLK GPIO5、CS GPIO6、DC GPIO7、RST GPIO15、BUSY GPIO16。

旧驱动中的 cancel callback、phase 状态、aborted error 和 aggressive interrupt 路径全部删除。公开接口只保留初始化、单色全刷、四灰阶全刷和休眠。刷新期间由调用线程同步等待 BUSY，超时返回明确错误。

### ink_input

迁移并收缩旧按键轮询逻辑。引脚为 Back GPIO9、Confirm GPIO10、Left GPIO12、Right GPIO11、Power GPIO46，均低电平有效。组件提供去抖、按下/释放和持续时间快照。

reader/photo 中 Back 持续 1200ms 后只触发一次返回 launcher。launcher 使用 Left/Right 切换选项，Confirm 进入选中固件。

### ink_sd

提供 `/sdcard` 的 SDMMC 4-bit 挂载和卸载。引脚为 CLK GPIO40、CMD GPIO39、D0 GPIO41、D1 GPIO42、D2 GPIO48、D3 GPIO38。无卡、空目录和挂载失败属于可显示错误，不触发崩溃。

### ink_epd_ui

提供单色 framebuffer、清屏、像素、矩形和小型内置 ASCII 字体。launcher 不依赖 SD 字体即可显示 `Reader` 和 `Photo`。reader/photo 的错误提示采用 ASCII 文本，保证字体文件缺失时仍可见。

### ink_fonts

从旧 `ink_cpfont` 拆取只读字体装载和文本绘制能力，去除任何 runtime/service 依赖。reader 会尝试从兼容路径加载 `.cpfont`；字体缺失不会阻止 XTC 页面显示。

### ink_reader_core

迁移旧 `ink_xtc_reader` 和 `ink_xtc_book` 中纯解析、页索引和页位图读取逻辑。第一阶段支持 `.xtc` 和 `.xtch`，优先扫描 `/sdcard/books`，并兼容 `/sdcard` 根目录。书籍内容是预渲染 480x800 单色页，翻页时直接读取当前页位图。

### ink_photo_core

迁移并净化旧 BMP catalog/parser。只扫描 `/sdcard/photos`，按文件名排序，只接受 `.bmp`。第一阶段支持旧项目最稳定的 480x800、无压缩、4bpp 索引 BMP，转换为 GDEY0426T82 两个灰阶位平面。

### ink_boot_switch

公开：

- `ink_boot_switch_to_launcher()`
- `ink_boot_switch_to_reader()`
- `ink_boot_switch_to_photo()`

内部通过 `esp_partition_find_first()` 按 app partition label 查找目标，调用 `esp_ota_set_boot_partition()`，成功后调用 `esp_restart()`。查找或设置失败时记录错误并返回，不执行重启。

## 固件行为

### launcher

启动日志为 `APP_START name=launcher`。初始化 EPD 和按键，显示 Reader/Photo。切换前分别记录：

- `BOOT_SWITCH from=launcher to=reader`
- `BOOT_SWITCH from=launcher to=photo`

launcher 不挂载 SD，不初始化 WiFi，不启动后台服务。

### reader

启动日志为 `APP_START name=reader`。初始化 EPD、按键，挂载 SD，尝试加载 cpfont，扫描书籍并打开第一本。Left/Right 翻页；没有可读书籍时显示 `NO BOOKS FOUND`。Back 长按记录 `BOOT_SWITCH from=reader to=launcher` 并返回 launcher。

### photo

启动日志为 `APP_START name=photo`。初始化 EPD、按键，挂载 SD，扫描 BMP 并显示第一张。Left/Right 循环切图；没有可读图片时显示 `NO PHOTOS FOUND`。Back 长按记录 `BOOT_SWITCH from=photo to=launcher` 并返回 launcher。

## 错误处理与日志约束

硬件初始化错误记录 `esp_err_to_name()`，并在仍可使用 EPD 时显示简短错误。SD 缺失、空目录、无匹配文件、字体缺失和单个文件损坏不会触发 `ESP_ERROR_CHECK`、assert 或 panic。

新工作区不引用 WiFi、voice note、ASR、I2S、TinyUSB、USB MSC、resource coordinator、background flush、runtime shell 或 display mailbox 组件，因此不会产生相关初始化日志。

## 测试与构建

纯逻辑使用宿主可执行测试或组件 self-test 覆盖：

- 按键去抖和长按单次触发
- boot partition label 映射
- XTC header/page index 解析
- XTC 页导航边界
- BMP header、palette 和像素平面转换
- 目录扩展名过滤和排序

每个切片执行红/绿验证，然后使用固定环境构建对应子项目：

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

构建能够验证源码、依赖、分区大小和镜像生成。真实 EPD、SD、按键及分区重启行为必须在连接设备后通过串口日志和屏幕表现验收；没有实机证据时不宣称硬件验收通过。

## 迁移边界

允许迁移并净化：GDEY0426T82 时序、framebuffer primitives、按键扫描、SDMMC mount、cpfont 读取、XTC 解析/页读取、BMP catalog/parser。

明确不迁移：旧 `app_main.c`、旧 runtime shell、app registry、display mailbox、aggressive interrupt、voice note、ASR、I2S、WiFi、USB MSC、resource coordinator、background flush 及其传递依赖。

## 完成条件

三个子项目均能在 ESP-IDF 5.5.4 下独立构建，镜像适配统一 16MB 分区表；源码和链接依赖中不存在排除模块。上板后按用户给出的十项功能与日志标准进行最终硬件验收。
