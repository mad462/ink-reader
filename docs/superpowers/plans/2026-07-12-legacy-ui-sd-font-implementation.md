# 旧版 UI、SD 字体与照片全刷对齐实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在三个独立固件架构不变的前提下，让 launcher、reader、photo 使用旧版 SD 目录、cpfont 中文渲染和可见 UI，并让照片预览始终执行完整四灰阶全刷。

**Architecture:** `ink_fonts` 从旧版提取同步 cpfont 文件加载、UTF-8 解码、字形缓存、测量和单色绘制；`ink_epd_ui` 只接收可选字体并绘制页面，不挂载 SD。各 app 自己挂载 SD、持有字体和业务资源；`ink_hw` 只补齐旧版灰阶命令顺序，不引入 runtime、mailbox、interrupt 或资源协调器。

**Tech Stack:** ESP-IDF 5.5.4、ESP32-S3、C、FreeRTOS、SDMMC/FATFS、GDEY0426T82 SPI、cpfont v4、PowerShell、esptool、COM9。

---

## 文件结构

- 创建 `components/ink_fonts/include/ink_cpfont.h`：公开同步 cpfont 数据结构和加载/绘制 API。
- 创建 `components/ink_fonts/ink_cpfont.c`：从旧版提取纯同步 cpfont v4 解析、UTF-8、字形缓存和 framebuffer 绘制。
- 修改 `components/ink_fonts/include/ink_fonts.h`：公开字体用途、候选路径加载、目录回退和 UTF-8 截断 API。
- 修改 `components/ink_fonts/ink_fonts.c`：实现旧版候选路径与 `/sdcard/fonts`、`/sdcard/.fonts` 递归搜索。
- 修改 `components/ink_fonts/CMakeLists.txt`：编译两个源文件并声明 heap/log 依赖。
- 修改 `components/ink_epd_ui/include/ink_epd_ui.h`：页面接口接受可选 cpfont，公开 launcher 几何常量。
- 修改 `components/ink_epd_ui/ink_epd_ui.c`：字体回退、旧版 header/两卡/icon/chevron、中文相册列表和状态页。
- 修改 `components/ink_epd_ui/CMakeLists.txt`：依赖 `ink_fonts`。
- 修改 `apps/launcher/main/CMakeLists.txt`、`apps/launcher/main/app_main.c`：只为字体挂载 SD，加载字体并绘制旧版两卡 launcher。
- 修改 `components/ink_reader_core/include/ink_reader_core.h`、`components/ink_reader_core/ink_reader_core.c`：返回目录/空目录/格式错误/成功分类。
- 修改 `apps/reader/main/app_main.c`：加载字体、打印扫描诊断并绘制明确状态。
- 修改 `apps/photo/main/CMakeLists.txt`、`apps/photo/main/app_main.c`：加载字体、按 codepoint 显示中文文件名并记录完整灰阶刷新。
- 修改 `components/ink_hw/ink_hw.c`：灰阶初始化补齐 `0x18/0x80` 并保留完整 native window、LUT 和双 plane 顺序。
- 修改 `docs/MIGRATION_NOTES.md`：逐项记录构建、烧录、串口和屏幕验收事实。

### Task 1：同步 cpfont 组件

**Files:**
- Create: `components/ink_fonts/include/ink_cpfont.h`
- Create: `components/ink_fonts/ink_cpfont.c`
- Modify: `components/ink_fonts/include/ink_fonts.h`
- Modify: `components/ink_fonts/ink_fonts.c`
- Modify: `components/ink_fonts/CMakeLists.txt`

- [ ] **Step 1: 写 RED API 与自测断言**

先在 `ink_fonts.h` 声明：

```c
typedef enum {
  INK_FONT_READER,
  INK_FONT_MENU,
  INK_FONT_FOOTER,
} ink_font_role_t;

bool ink_fonts_load(ink_cpfont_t *font, ink_font_role_t role);
size_t ink_fonts_utf8_codepoint_count(const char *text);
bool ink_fonts_utf8_truncate_tail(const char *src, char *dst,
                                  size_t dst_size, size_t max_codepoints);
```

在 `ink_fonts_self_test()` 增加 `"相册A"` 为 3 个 codepoint 的断言，并用 5 个 codepoint 的 `"相册ABC"` 验证 `max_codepoints=4` 时截为 `"相..."` 且输出保持合法 UTF-8。此时不实现 helper，运行：

```powershell
. .\tools\idf_env.ps1
idf.py -C .\apps\reader build
```

预期：FAIL，链接器报告 `ink_fonts_utf8_codepoint_count` 或 `ink_fonts_utf8_truncate_tail` 未定义。

- [ ] **Step 2: 提取 cpfont 同步核心**

从只读旧文件 `D:\FUCKIDF\ink-reader\components\ink_hw\ink_cpfont.[ch]` 迁移以下 API，名称保持不变：

```c
void ink_cpfont_init(ink_cpfont_t *font);
void ink_cpfont_close(ink_cpfont_t *font);
esp_err_t ink_cpfont_load(ink_cpfont_t *font, const char *path);
bool ink_cpfont_is_loaded(const ink_cpfont_t *font);
esp_err_t ink_cpfont_draw_text_bw_scaled(ink_cpfont_t *font,
                                         uint8_t *buffer,
                                         int x, int top_y,
                                         const char *text,
                                         uint8_t scale_divisor,
                                         int *out_width_px);
bool ink_cpfont_self_test(void);
```

保留同步 `FILE *` 读取、interval/glyph 查找、2bit 到 1bit 转换和固定小缓存。删除 `read_at` callback 及其测试；不包含 SDIO job、runtime 或 coordinator。把 `epd_gdey0426t82.h` 依赖替换为组件内常量：

```c
enum { INK_CPFONT_FB_WIDTH = 480, INK_CPFONT_FB_HEIGHT = 800 };
```

所有大缓冲继续从 PSRAM/heap 分配；`ink_cpfont_self_test()` 的栈上渲染缓冲不得超过 1920 bytes。

- [ ] **Step 3: 实现候选路径和目录回退**

按 role 使用设计文档中的旧版路径顺序。固定路径全部失败后，只扫描：

```text
/sdcard/fonts
/sdcard/FONTS
/sdcard/.fonts/LXGWWenKai
/sdcard/.fonts/NotoSansSC
```

并额外递归一层 `/sdcard/.fonts/<family>/*.cpfont`。只接受 `.cpfont`，按不区分大小写的文件名排序，逐个同步调用 `ink_cpfont_load()`；日志打印每个失败路径和最终成功路径。

- [ ] **Step 4: 完成 UTF-8 helper 并验证 GREEN**

解码必须验证 continuation byte、拒绝 overlong/surrogate/大于 `U+10FFFF`，非法序列按单字节替换字符前进，不能越界读取。尾截断按 codepoint 输出，超长时保留 `max_codepoints - 3` 个 codepoint 并追加 ASCII `...`。

运行：

```powershell
. .\tools\idf_env.ps1
idf.py -C .\apps\reader build
rg -n -i "sdio|resource_coordinator|background|runtime|mailbox|interrupt" components/ink_fonts
```

预期：reader 构建通过；扫描无禁用依赖命中。

- [ ] **Step 5: 检查栈帧并提交**

```powershell
$objdump='C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin\xtensa-esp32s3-elf-objdump.exe'
& $objdump -d .\apps\reader\build\esp-idf\ink_fonts\libink_fonts.a | Select-String '<ink_cpfont_self_test>|entry'
git diff --check
git add components/ink_fonts
git commit -m "字体：迁移同步cpfont渲染核心"
```

预期：构建无栈溢出警告，提交只包含字体组件。

### Task 2：`ink_epd_ui` 可选字体与 UTF-8 页面绘制

**Files:**
- Modify: `components/ink_epd_ui/include/ink_epd_ui.h`
- Modify: `components/ink_epd_ui/ink_epd_ui.c`
- Modify: `components/ink_epd_ui/CMakeLists.txt`

- [ ] **Step 1: 用新接口制造 RED**

增加页面字体集合：

```c
typedef struct {
  ink_cpfont_t *title;
  ink_cpfont_t *body;
  ink_cpfont_t *footer;
} ink_epd_ui_fonts_t;
```

将页面接口改为接收 `const ink_epd_ui_fonts_t *fonts`。在 self-test 中调用尚未实现的 `ink_epd_ui_measure_text()`，断言 cpfont 为 NULL 时 `"ABC"` 的 ASCII 2x 宽度为 36px。运行 launcher build，预期 FAIL：函数未定义或签名未迁移完成。

- [ ] **Step 2: 实现字体优先、ASCII 回退**

统一 helper：cpfont loaded 时调用 `ink_cpfont_draw_text_bw_scaled()`；未加载时仅当整个字符串为 ASCII 才使用 5x7 字体。包含非 ASCII 且无 cpfont 时不逐字节绘制问号，返回 false 并由 app 选择英文 fallback。

```c
bool ink_epd_ui_draw_text_font(uint8_t *buffer, size_t length,
                               ink_cpfont_t *font, int x, int y,
                               uint8_t scale_divisor,
                               const char *text, int *out_width);
```

测量模式允许 `buffer == NULL`，不得写 framebuffer。

- [ ] **Step 3: 改造状态页和相册列表**

`ink_epd_ui_draw_status()`、`ink_epd_ui_draw_photo_list()` 使用可选字体。相册条目先用 `ink_fonts_utf8_truncate_tail()` 按 codepoint 截断，再测量并预留右侧计数；不得用 `strlen()` 估算中文宽度。无字体时页面显示英文 `PHOTO ALBUM`、`NO PHOTOS FOUND`，不显示乱码。

- [ ] **Step 4: self-test 与构建**

self-test 至少验证：ASCII fallback、非 ASCII 无字体不绘制 `?`、中文截断后的字节序列完整、原有 photo 选择区域不变。运行：

```powershell
. .\tools\idf_env.ps1
idf.py -C .\apps\photo build
```

预期：photo 构建通过，`ink_epd_ui_self_test()` 返回 true。

- [ ] **Step 5: 提交**

```powershell
git add components/ink_epd_ui
git commit -m "界面：支持cpfont与UTF-8文本"
```

### Task 3：launcher 旧版 header、两卡、图标和横线局刷

**Files:**
- Modify: `components/ink_epd_ui/include/ink_epd_ui.h`
- Modify: `components/ink_epd_ui/ink_epd_ui.c`
- Modify: `apps/launcher/main/CMakeLists.txt`
- Modify: `apps/launcher/main/app_main.c`

- [ ] **Step 1: 写 launcher 几何 RED 断言**

公开并断言：header gutter=24、divider y=38、list x=24、width=432、row height=70、gap=6、首行 y=50、次行 y=126。self-test 还需检查 book/photo icon 和 chevron 所在区域存在黑像素。先写断言再改绘制，运行 launcher build，预期 self-test 在实机启动时会失败；同时用编译期 `_Static_assert` 锁定几何常量以便 build 阶段捕获偏差。

- [ ] **Step 2: 迁移旧版纯 launcher 绘制**

只从 `ink_app_render.c` 和 `epd_test_pattern.c` 提取以下纯绘制：

```text
crosspoint header：gutter 24、title y 8、divider y 38
list：x 24、y 50、width 432、row 70、gap 6
Reader：book icon
Photo：photo icon
右侧：7px chevron
```

两行标题/说明使用旧版文案；选中仍为左侧 `4px` 横线标记，不画黑底。`ink_epd_ui_launcher_selection_region()` 返回旧、新华线的联合区域，并包含清除旧标记所需白底。

- [ ] **Step 3: launcher 只为字体挂载 SD**

在 `app_main.c` 初始化 EPD/按键后挂载 SD，分别加载 menu/footer font；不调用 reader/photo catalog。挂载或字体失败时记录日志并使用英文卡片 fallback。首次全刷，选择变化仍区域局刷；切换日志保持原格式。

同时给 launcher main 增加 `ink_fonts`、`ink_sd` 依赖，不添加 WiFi/USB 组件。

- [ ] **Step 4: 构建、静态检查和提交**

```powershell
. .\tools\idf_env.ps1
idf.py -C .\apps\launcher build
rg -n -i "ink_reader|ink_photo|esp_wifi|tinyusb|usb_msc" apps/launcher components/ink_epd_ui --glob '*.[ch]' --glob 'CMakeLists.txt'
git diff --check
git add components/ink_epd_ui apps/launcher
git commit -m "界面：对齐旧版启动器两卡布局"
```

预期：launcher 构建通过；launcher 不扫描书籍/图片且无禁用模块。

### Task 4：reader 扫描结果分类和字体状态页

**Files:**
- Modify: `components/ink_reader_core/include/ink_reader_core.h`
- Modify: `components/ink_reader_core/ink_reader_core.c`
- Modify: `apps/reader/main/app_main.c`

- [ ] **Step 1: 写扫描分类 RED 测试**

增加：

```c
typedef enum {
  INK_READER_SCAN_OK,
  INK_READER_SCAN_DIR_MISSING,
  INK_READER_SCAN_EMPTY,
  INK_READER_SCAN_FORMAT_ERROR,
  INK_READER_SCAN_IO_ERROR,
} ink_reader_scan_result_t;

ink_reader_scan_result_t ink_reader_open_first_book(
    ink_reader_book_t *book, char *candidate_path, size_t path_size);
```

把目录遍历依赖收敛到内部 helper，使 self-test 可用临时目录验证：目录缺失、只有非 XTC 文件、坏 header、第二个候选有效。先写测试并调用未实现函数，构建 reader，预期 FAIL。

- [ ] **Step 2: 实现“继续扫描直到成功”**

顺序扫描 `/sdcard/books`，再扫描 `/sdcard` 根目录；只接收 `.xtc/.xtch`。候选按不区分大小写排序。一个候选解析失败时继续下一个；至少一个候选存在但全部失败返回 `FORMAT_ERROR`，无候选返回 `EMPTY`，`opendir` 的 `ENOENT` 与其他 errno 分开。

每种结果打印独立日志，成功日志包含 path/pages；不得创建、移动或修改 SD 文件。

- [ ] **Step 3: reader 接入字体和明确状态**

SD 成功后加载 reader/menu font。状态映射：

```text
SD mount 失败 -> SD CARD ERROR
目录缺失 -> BOOKS DIRECTORY MISSING
目录空/无匹配格式 -> NO BOOKS FOUND
候选均无效 -> BOOK FORMAT ERROR
I/O 错误 -> BOOK SCAN ERROR
```

中文字体存在时可使用中文标题/消息；不存在时使用以上英文。书页继续直接显示 XTG/XTH framebuffer，不重新排版。Back 短按逻辑不变。

- [ ] **Step 4: 构建并提交**

```powershell
. .\tools\idf_env.ps1
idf.py -C .\apps\reader build
git diff --check
git add components/ink_reader_core apps/reader/main/app_main.c
git commit -m "阅读器：区分SD书籍扫描结果"
```

### Task 5：photo 中文列表与完整灰阶全刷

**Files:**
- Modify: `apps/photo/main/CMakeLists.txt`
- Modify: `apps/photo/main/app_main.c`
- Modify: `components/ink_hw/ink_hw.c`

- [ ] **Step 1: 写灰阶命令顺序 RED 检查**

把灰阶初始化中的关键命令列表提取为纯常量/helper，self-test 断言顺序包含：

```c
0x12, 0x0c, 0x01, 0x3c, 0x18, 0x44, 0x45, 0x4e, 0x4f,
0x20, /* LUT activate */
0x26, /* MSB */
0x24, /* LSB */
0x22, 0x20
```

并断言 `0x18` 后的数据为 `0x80`、最终 update mode 为 `0xC7`。先增加断言，当前 gray path 缺少 `0x18/0x80`，预期 RED。

- [ ] **Step 2: 对齐旧版灰阶初始化**

在 `init_sequence(true)` 中按旧版顺序补上：

```c
ESP_RETURN_ON_ERROR(command(0x18), TAG, "gray temp cmd");
ESP_RETURN_ON_ERROR(data_byte(0x80), TAG, "gray temp data");
```

确保完整 native window 仍为 `800x480` 像素坐标，不得改成 `0..99`。`ink_hw_gray_refresh()` 顺序保持 MSB `0x26`、LSB `0x24`、`0x22/0xC7`、`0x20`、BUSY wait。预览不得调用 `ink_hw_partial_refresh_area()`。

- [ ] **Step 3: photo 加载字体并保持状态提交语义**

photo main 依赖 `ink_fonts`，SD 挂载后加载 menu/footer font，把字体传给相册列表/错误页。中文文件名由 UI 按 codepoint 截断；catalog path/name 不做字节破坏。

每次 LIST -> PREVIEW 和 PREVIEW 左右切图前打印：

```c
ESP_LOGI(TAG, "PHOTO_REFRESH mode=full_gray");
```

仅 `ink_hw_gray_refresh()` 成功后提交预览 index/view；失败保持原已提交状态。PREVIEW Confirm 返回列表仍执行单色全刷，Back 在任一视图短按返回 launcher。

- [ ] **Step 4: 构建、调用点检查和提交**

```powershell
. .\tools\idf_env.ps1
idf.py -C .\apps\photo build
rg -n "PHOTO_REFRESH mode=full_gray|ink_hw_gray_refresh|ink_hw_partial_refresh_area" apps/photo/main/app_main.c
git diff --check
git add components/ink_hw apps/photo
git commit -m "相册：修复中文列表并补齐灰阶全刷"
```

预期：photo 构建通过；局刷调用只在 LIST 导航路径，预览路径只有 gray refresh。

### Task 6：逐项目构建与架构排除项检查

**Files:**
- Modify: `docs/MIGRATION_NOTES.md`

- [ ] **Step 1: 完整构建**

```powershell
& .\tools\build_all.ps1
```

预期：launcher、reader、photo 依次输出 `Project build complete`，脚本退出码 0；记录三个 `.bin` 的实际字节数。

- [ ] **Step 2: 排除禁用模块和栈风险**

```powershell
$patterns='voice_note|\bASR\b|i2s_|esp_wifi|tinyusb|usb_msc|ink_resource_coordinator|ink_background_flush|runtime_shell|display_mailbox|aggressive_interrupt'
rg -n -i $patterns apps components --glob '*.[ch]' --glob 'CMakeLists.txt'
git diff --check
```

预期：`rg` 退出码 1（无命中），diff 检查无输出。用 objdump 检查三个 `app_main` 和字体 self-test 的栈帧，任何单帧不得接近 3584B 主任务栈。

- [ ] **Step 3: 更新迁移记录并提交**

只记录实际完成的构建和静态检查，不把尚未烧录/观察的项目写成 PASS：

```powershell
git add docs/MIGRATION_NOTES.md
git commit -m "文档：记录旧版UI与字体迁移构建"
```

### Task 7：COM9 烧录与实体交互验收

**Files:**
- Modify: `docs/MIGRATION_NOTES.md`

- [ ] **Step 1: 确认 COM9 空闲并烧录三镜像**

```powershell
& .\tools\flash_launcher.ps1 -Port COM9
& .\tools\flash_reader.ps1 -Port COM9
& .\tools\flash_photo.ps1 -Port COM9
```

每条命令必须包含 `Hash of data verified`；launcher 写完整布局，reader 写 `0x120000`，photo 写 `0x520000`。

- [ ] **Step 2: launcher 实机验收**

启动 launcher，串口必须出现 `APP_START name=launcher`。屏幕检查旧版 header、前两行卡片、book/photo icon、chevron、左侧横线；左右选择只局刷标记区域，不全屏反白。确认 launcher 仅出现 SD/font 日志，不出现业务 catalog 和禁用模块日志。

- [ ] **Step 3: reader 实机验收**

Confirm 进入 reader，确认 `APP_START name=reader` 和 `BOOT_SWITCH from=launcher to=reader`。SD 有有效 XTC/XTCH 时记录 path/pages 并翻页；无书或格式错误时记录准确状态。Back 短按返回 launcher。

- [ ] **Step 4: photo 实机验收**

Confirm 进入 photo，中文文件名不得显示问号乱码。列表左右局刷；打开照片和预览左右切图时串口每次出现 `PHOTO_REFRESH mode=full_gray`，屏幕执行完整全刷；Confirm 返回列表全刷，Back 短按返回 launcher。

- [ ] **Step 5: 日志禁用项与最终提交**

串口观察至少覆盖三个 app 的启动和一次往返。不得出现：

```text
panic
assert failed
Guru Meditation
sdmmc_read_sectors: not enough mem
panel not ready after sw reset
epd busy_wait aborted
WiFi / voice_note / I2S / ASR / USB MSC
```

把实际 PASS/FAIL 和未测项写入迁移记录：

```powershell
git add docs/MIGRATION_NOTES.md
git commit -m "验证：记录COM9旧版交互验收"
git status --short
```

全部实机项通过后才进入最终代码审查和合并决策。
