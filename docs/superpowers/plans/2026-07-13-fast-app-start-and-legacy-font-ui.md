# 快速应用启动与旧版字号 UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消除进入/退出 reader 时的伪字体错误，缩短 launcher/reader/photo 首个有效画面的等待，并恢复旧固件字号、launcher 横线位置与 photo 首屏交互。

**Architecture:** launcher 和固定状态文案使用从旧版 LXGW 字体生成的 flash 位图短语，不挂载 SD；reader 的预渲染书页不加载 cpfont；photo 先解码首张有效 BMP，再让一次性字体任务与 EPD 灰阶全刷并行。动态图片名仍由 SD cpfont 渲染，三个 app 继续是互斥的独立固件。

**Tech Stack:** ESP-IDF 5.5.4、ESP32-S3、FreeRTOS Event Groups、FATFS/SDMMC、GDEY0426T82、Python 3/Pillow、TinyCC host tests、PowerShell。

---

## 文件结构

- Create `tools/generate_ui_text_bitmaps.py`: 从指定 TTF 和固定短语清单生成 1bpp C 资产。
- Create `tools/tests/test_generate_ui_text_bitmaps.py`: 验证短语清单、输出稳定性和非空位图。
- Create `components/ink_epd_ui/ink_ui_text_assets.h`: 固化短语查询接口。
- Create `components/ink_epd_ui/ink_ui_text_assets.c`: 生成的 flash 字形/短语位图。
- Modify `components/ink_epd_ui/CMakeLists.txt`: 编译固化文字资产。
- Modify `components/ink_epd_ui/ink_epd_ui.c`: 固化文字 fallback、旧版字号、launcher 横线与 photo 列表字号。
- Modify `components/ink_epd_ui/include/ink_epd_ui.h`: 导出稳定的 launcher 标记几何常量供测试。
- Modify `components/ink_fonts/ink_fonts.c`: `stat()` 后才尝试打开候选字体。
- Modify `apps/launcher/main/app_main.c`: 移除 SD/cpfont 初始化并加入阶段计时。
- Modify `apps/launcher/main/CMakeLists.txt`: 移除 launcher 对 `ink_fonts`、`ink_sd` 的直接依赖。
- Modify `apps/reader/main/app_main.c`: 移除两套 cpfont 加载，缩小状态页并加入阶段计时。
- Modify `apps/reader/main/CMakeLists.txt`: 移除 reader 对 `ink_fonts` 的直接依赖。
- Modify `components/ink_photo_core/include/ink_photo_core.h`: 增加可测试的“最多扫描一圈并跳过坏图”接口。
- Modify `components/ink_photo_core/ink_photo_core.c`: 实现首张/下一张可解码项查找。
- Modify `components/ink_photo_core/test/photo_palette_host_test.c`: 覆盖空目录、连续坏图、全部坏图和环绕。
- Modify `apps/photo/main/app_main.c`: 默认 PREVIEW、首图解码、一次性字体任务、同步 fallback 与阶段计时。
- Modify `docs/MIGRATION_NOTES.md`: 记录性能根因、行为变化、构建和 COM9 验收结果。

### Task 1: 生成并验证固化 UI 短语资产

**Files:**
- Create: `tools/generate_ui_text_bitmaps.py`
- Create: `tools/tests/test_generate_ui_text_bitmaps.py`
- Create: `components/ink_epd_ui/ink_ui_text_assets.h`
- Create: `components/ink_epd_ui/ink_ui_text_assets.c`
- Modify: `components/ink_epd_ui/CMakeLists.txt`

- [ ] **Step 1: 写生成器失败测试**

测试以临时输出路径调用 `generate()`，要求固定短语至少包含启动器、书库、相册、加载和错误状态，并检查每条位图都有黑色像素：

```python
import re
from pathlib import Path
from tools.generate_ui_text_bitmaps import PHRASES, generate


def test_fixed_ui_phrases_generate_nonempty_c(tmp_path: Path) -> None:
    required = {"启动器", "书库", "相册", "正在加载", "未找到图片",
                "未找到可显示图片", "未找到书籍"}
    assert required <= {item.text for item in PHRASES}
    output = tmp_path / "ink_ui_text_assets.c"
    generate(Path(r"D:\FUCKIDF\ink-reader\tools\ebook-canvas-studio\fonts\LXGWWenKai-Regular.ttf"), output)
    source = output.read_text(encoding="utf-8")
    assert "const ink_ui_text_asset_t ink_ui_text_assets[]" in source
    bytes_written = re.findall(r"0x([0-9a-fA-F]{2})", source)
    assert bytes_written and any(int(value, 16) != 0 for value in bytes_written)
```

- [ ] **Step 2: 运行测试确认 RED**

Run:

```powershell
python -m pytest tools/tests/test_generate_ui_text_bitmaps.py -q
```

Expected: FAIL，原因是 `tools.generate_ui_text_bitmaps` 尚不存在。

- [ ] **Step 3: 实现最小生成器和查询接口**

生成器固定输出以下记录类型，并以 Pillow `ImageFont.truetype()`、`textbbox()`、阈值 128 生成逐行 MSB-first 1bpp 数据：

```c
typedef struct {
  const char *text;
  uint8_t pixel_size;
  uint16_t width;
  uint16_t height;
  const uint8_t *bitmap;
} ink_ui_text_asset_t;

const ink_ui_text_asset_t *ink_ui_text_asset_find(const char *text,
                                                   uint8_t pixel_size);
```

固定清单使用明确字号：

```python
PHRASES = (
    Phrase("启动器", 24), Phrase("阅读 / 相册", 16),
    Phrase("书库", 12), Phrase("相册", 12),
    Phrase("打开图书与最近阅读", 16), Phrase("浏览 TF 卡灰阶图片", 16),
    Phrase("正在加载", 16), Phrase("字体不可用", 16),
    Phrase("未找到图片", 16), Phrase("未找到可显示图片", 16),
    Phrase("未找到书籍", 16), Phrase("SD 卡错误", 16),
    Phrase("书籍格式错误", 16),
)
```

生成并编译资产：

```powershell
python tools/generate_ui_text_bitmaps.py `
  --font 'D:\FUCKIDF\ink-reader\tools\ebook-canvas-studio\fonts\LXGWWenKai-Regular.ttf' `
  --output components\ink_epd_ui\ink_ui_text_assets.c
```

`components/ink_epd_ui/CMakeLists.txt` 的 `SRCS` 改为：

```cmake
idf_component_register(SRCS "ink_epd_ui.c" "ink_ui_text_assets.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ink_fonts)
```

- [ ] **Step 4: 运行测试确认 GREEN**

Run:

```powershell
python -m pytest tools/tests/test_generate_ui_text_bitmaps.py -q
```

Expected: PASS，且重新运行生成器后 `git diff --exit-code components/ink_epd_ui/ink_ui_text_assets.c` 成功。

- [ ] **Step 5: 提交**

```powershell
git add tools/generate_ui_text_bitmaps.py tools/tests/test_generate_ui_text_bitmaps.py components/ink_epd_ui/ink_ui_text_assets.h components/ink_epd_ui/ink_ui_text_assets.c components/ink_epd_ui/CMakeLists.txt
git commit -m "字体：固化固定界面中文字形"
```

### Task 2: 恢复旧版字号与 launcher 横线几何

**Files:**
- Modify: `components/ink_epd_ui/include/ink_epd_ui.h`
- Modify: `components/ink_epd_ui/ink_epd_ui.c`

- [ ] **Step 1: 先修改自检期望使当前实现失败**

在 `ink_epd_ui_self_test()` 中将 launcher 标记断言改为新几何，并增加固化中文绘制与 photo divisor 的断言：

```c
const ink_epd_region_t moved = ink_epd_ui_launcher_selection_region(0, 1);
if (moved.x != 22 || moved.y != 79 || moved.width != 20 ||
    moved.height != 88) return false;

/* selected marker: x=26..37, y=row_y+33..36 */
for (int y = 33; y < 37; ++y)
  for (int x = 26; x < 38; ++x)
    if (!pixel_is_black(buffer, x, INK_LAUNCHER_LIST_Y + y)) return false;
```

这里 region 含 4px padding：单行半开区间为 `x=[22,42), y=[79,91)`；跨两行半开区间为 `x=[22,42), y=[79,167)`。

- [ ] **Step 2: 构建 launcher 并在临时诊断入口运行 self-test，确认 RED**

Run:

```powershell
. .\tools\idf_env.ps1
idf.py -C apps\launcher build
.\tools\flash_launcher.ps1 -Port COM9
idf.py -C apps\launcher -p COM9 monitor
```

Expected: 串口出现 `component self test failed`，证明旧 `x=68,y=row+20` 实现不能满足新断言；退出 monitor 使用 `Ctrl+]`。

- [ ] **Step 3: 实现固化文字绘制、横线和旧版字号**

增加 `draw_builtin_text(buffer, length, x, y, text, pixel_size)`，按明确的像素字号匹配短语资产后逐像素 blit。launcher 和固定状态页显式调用 24/16/12px 固化短语；`ink_epd_ui_draw_text_font()` 继续只处理“已加载 cpfont -> ASCII”，不从 ASCII scale 猜测中文字号。launcher 常量改为：

```c
static const int kLauncherLineX = 26;
static const int kLauncherLineWidth = 12;
static const int kLauncherLineHeight = 4;
static const int kLauncherLineYOffset =
    (INK_LAUNCHER_ROW_HEIGHT - kLauncherLineHeight) / 2;
```

launcher 使用固化 24/16/12px 文案；动态 photo 文件名的测量和绘制均传 `font_scale_divisor=2U`：

```c
ink_epd_ui_measure_text(body_font, title, 2, 2U, &width);
ink_epd_ui_draw_text_font(buffer, length, body_font, text_x,
                          y + kPhotoListTitleYOffset, 2, 2U, title, NULL);
```

photo header 保持 divisor 1，footer/计数保持 divisor 1。状态页标题和正文改为小号布局，不再使用 ASCII scale 4/3。

- [ ] **Step 4: 构建、烧录并确认 GREEN**

Run:

```powershell
idf.py -C apps\launcher build
.\tools\flash_launcher.ps1 -Port COM9
idf.py -C apps\launcher -p COM9 monitor
```

Expected: 不出现 `component self test failed`；launcher 标题、卡片文案为中文，横线位于图标左侧且垂直居中。

- [ ] **Step 5: 提交**

```powershell
git add components/ink_epd_ui/include/ink_epd_ui.h components/ink_epd_ui/ink_epd_ui.c
git commit -m "界面：恢复旧版字号与启动器选中横线"
```

### Task 3: 静默跳过不存在的字体候选

**Files:**
- Modify: `components/ink_fonts/ink_fonts.c`
- Create: `tools/tests/test_font_candidate_policy.py`

- [ ] **Step 1: 增加失败契约测试**

验证 `try_load_path()` 必须在 `ink_cpfont_load()` 之前执行 `stat()`，并把缺失候选记为 DEBUG：

```python
from pathlib import Path


def test_missing_font_candidate_is_filtered_before_cpfont_load() -> None:
    text = Path("components/ink_fonts/ink_fonts.c").read_text(encoding="utf-8")
    start = text.index("static bool try_load_path")
    end = text.index("static bool load_from_directory", start)
    body = text[start:end]
    assert body.index("stat(path") < body.index("ink_cpfont_load(font, path)")
    assert 'ESP_LOGD(TAG, "font candidate missing path=%s"' in body
```

- [ ] **Step 2: 运行确认 RED**

Run:

```powershell
python -m pytest tools/tests/test_font_candidate_policy.py -q
```

Expected: FAIL，因为当前 `try_load_path()` 直接调用 `ink_cpfont_load()`。

- [ ] **Step 3: 实现 stat 前置检查**

`try_load_path()` 在 `ink_cpfont_load()` 前执行：

```c
struct stat st;
if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
  ESP_LOGD(TAG, "font candidate missing path=%s", path);
  return false;
}
```

存在但加载失败时保留 `ESP_LOGW`；所有候选失败后仍只打印一次 `font not found role=%d`。

- [ ] **Step 4: 测试并 build photo 确认 GREEN**

Run:

```powershell
python -m pytest tools/tests/test_font_candidate_policy.py -q
. .\tools\idf_env.ps1
idf.py -C apps\photo build
```

Expected: pytest PASS，photo `Project build complete`。不存在候选不再打印 WARN 的串口语义留在 Task 7 实机验证。

- [ ] **Step 5: 提交**

```powershell
git add components/ink_fonts/ink_fonts.c tools/tests/test_font_candidate_policy.py
git commit -m "字体：静默跳过不存在的候选路径"
```

### Task 4: 精简 launcher 与 reader 启动链路

**Files:**
- Modify: `apps/launcher/main/app_main.c`
- Modify: `apps/launcher/main/CMakeLists.txt`
- Modify: `apps/reader/main/app_main.c`
- Modify: `apps/reader/main/CMakeLists.txt`

- [ ] **Step 1: 写静态约束失败测试**

新增 `tools/tests/test_app_startup_contracts.py`，直接检查 launcher/reader 源码不再调用 SD 字体加载：

```python
from pathlib import Path


def source(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def test_launcher_has_no_sd_or_cpfont_startup() -> None:
    text = source("apps/launcher/main/app_main.c")
    assert "ink_sd_mount(" not in text
    assert "ink_fonts_load(" not in text


def test_reader_does_not_load_fonts_for_prerendered_pages() -> None:
    text = source("apps/reader/main/app_main.c")
    assert "ink_fonts_load(" not in text
```

- [ ] **Step 2: 运行确认 RED**

Run:

```powershell
python -m pytest tools/tests/test_app_startup_contracts.py -q
```

Expected: 两个测试都失败。

- [ ] **Step 3: 移除启动字体加载并加入阶段计时**

launcher 删除 `ink_fonts.h`、`ink_sd.h`、两个静态 font 和 mount/load 代码，直接调用：

```c
ink_epd_ui_draw_launcher(framebuffer, INK_EPD_BUFFER_SIZE, selected);
```

reader 删除两个静态 font 和 load 代码；书页仍按原路径解码，错误页改用固化短语：

```c
const char *message = "未找到书籍";
if (sd_ret != ESP_OK) message = "SD 卡错误";
else if (scan_result == INK_READER_SCAN_FORMAT_ERROR) message = "书籍格式错误";
ink_epd_ui_draw_status(framebuffer, INK_EPD_BUFFER_SIZE, "READER", message);
```

两个 app 使用统一格式记录累计毫秒：

```c
ESP_LOGI(TAG, "APP_STAGE name=%s stage=%s elapsed_ms=%lld",
         app_name, stage, (esp_timer_get_time() - started_us) / 1000);
```

launcher CMake `REQUIRES` 去掉 `ink_fonts ink_sd`；reader CMake 去掉直接的 `ink_fonts`。

- [ ] **Step 4: 测试并分别 build**

Run:

```powershell
python -m pytest tools/tests/test_app_startup_contracts.py -q
. .\tools\idf_env.ps1
idf.py -C apps\launcher build
idf.py -C apps\reader build
```

Expected: pytest PASS；两个 app 均 `Project build complete`。

- [ ] **Step 5: 提交**

```powershell
git add tools/tests/test_app_startup_contracts.py apps/launcher/main/app_main.c apps/launcher/main/CMakeLists.txt apps/reader/main/app_main.c apps/reader/main/CMakeLists.txt
git commit -m "启动：移除启动器与阅读器冗余字体加载"
```

### Task 5: 用纯策略实现坏图跳过与环绕

**Files:**
- Modify: `components/ink_photo_core/include/ink_photo_core.h`
- Modify: `components/ink_photo_core/ink_photo_core.c`
- Modify: `components/ink_photo_core/test/photo_palette_host_test.c`

- [ ] **Step 1: 写失败 host tests**

定义生产可用的解码回调边界：

```c
typedef bool (*ink_photo_try_item_fn)(const ink_photo_item_t *item,
                                     void *context);
bool ink_photo_catalog_find_decodable(const ink_photo_catalog_t *catalog,
                                      size_t first_index, int direction,
                                      ink_photo_try_item_fn try_item,
                                      void *context, size_t *out_index);
```

host test 使用 `bool decodable[4]` 回调，断言：空 catalog 调用 0 次；`false,false,true` 返回 2；全部 false 恰好调用 count 次；从末尾向右和从 0 向左都正确环绕。

- [ ] **Step 2: 运行确认 RED**

Run:

```powershell
.\components\ink_photo_core\test\run_host_tests.ps1
```

Expected: 编译失败，提示 `ink_photo_catalog_find_decodable` 未定义。

- [ ] **Step 3: 实现最多扫描一圈的策略**

```c
bool ink_photo_catalog_find_decodable(const ink_photo_catalog_t *catalog,
                                      size_t first_index, int direction,
                                      ink_photo_try_item_fn try_item,
                                      void *context, size_t *out_index) {
  if (!catalog || catalog->count == 0 || !try_item || !out_index ||
      (direction != -1 && direction != 1)) return false;
  size_t index = first_index % catalog->count;
  for (size_t checked = 0; checked < catalog->count; ++checked) {
    if (try_item(&catalog->items[index], context)) {
      *out_index = index;
      return true;
    }
    index = direction > 0 ? (index + 1) % catalog->count
                          : (index + catalog->count - 1) % catalog->count;
  }
  return false;
}
```

- [ ] **Step 4: 运行确认 GREEN**

Run:

```powershell
.\components\ink_photo_core\test\run_host_tests.ps1
```

Expected: `PASS: photo palette host tests`。

- [ ] **Step 5: 提交**

```powershell
git add components/ink_photo_core/include/ink_photo_core.h components/ink_photo_core/ink_photo_core.c components/ink_photo_core/test/photo_palette_host_test.c
git commit -m "相册：跳过损坏图片并限制扫描一圈"
```

### Task 6: Photo 默认预览并并行加载字体

**Files:**
- Modify: `apps/photo/main/app_main.c`
- Modify: `tools/tests/test_app_startup_contracts.py`

- [ ] **Step 1: 增加失败策略测试**

扩展静态约束测试，要求默认 view 为 PREVIEW、存在一次性任务和 Event Group、字体任务不调用任何 EPD API：

```python
def test_photo_starts_in_preview_and_font_task_never_touches_epd() -> None:
    text = source("apps/photo/main/app_main.c")
    assert "enum photo_view view = PREVIEW;" in text
    assert "xTaskCreate" in text
    assert "xEventGroupSetBits" in text
    task = text[text.index("static void photo_font_task"):
                text.index("void app_main")]
    assert "ink_hw_" not in task
```

- [ ] **Step 2: 运行确认 RED**

Run:

```powershell
python -m pytest tools/tests/test_app_startup_contracts.py -q
```

Expected: photo 测试失败，因为当前默认 `LIST` 且没有字体任务。

- [ ] **Step 3: 拆分 decode/refresh 并接入首张有效图片**

使用小型 context 让 core 回调写入现有 planes：

```c
typedef struct { uint8_t *lsb; uint8_t *msb; } photo_decode_context_t;

static bool try_decode_item(const ink_photo_item_t *item, void *context) {
  photo_decode_context_t *decode = context;
  return ink_photo_decode_bmp(item->path, decode->lsb, INK_PHOTO_PLANE_SIZE,
                              decode->msb, INK_PHOTO_PLANE_SIZE);
}
```

catalog count 为 0 时显示“未找到图片”；查找一圈失败时显示“未找到可显示图片”；成功时保存返回 index，启动字体任务，然后调用 `ink_hw_gray_refresh()`。

- [ ] **Step 4: 实现一次性字体任务与同步 fallback**

使用一个 Event Group，两位状态足够：

```c
enum { PHOTO_FONT_DONE = BIT0, PHOTO_FONT_READY = BIT1 };

static void load_photo_fonts(void) {
  bool menu_ok = ink_fonts_load(&s_menu_font, INK_FONT_MENU);
  bool footer_ok = ink_fonts_load(&s_footer_font, INK_FONT_FOOTER);
  if (menu_ok) {
    s_photo_fonts.title = &s_menu_font;
    s_photo_fonts.body = &s_menu_font;
  }
  if (footer_ok) s_photo_fonts.footer = &s_footer_font;
  xEventGroupSetBits(s_font_events,
                     PHOTO_FONT_DONE | (menu_ok ? PHOTO_FONT_READY : 0));
}

static void photo_font_task(void *unused) {
  ESP_LOGI(TAG, "APP_STAGE name=photo stage=font_start elapsed_ms=%lld",
           elapsed_ms());
  load_photo_fonts();
  ESP_LOGI(TAG, "APP_STAGE name=photo stage=font_done elapsed_ms=%lld",
           elapsed_ms());
  vTaskDelete(NULL);
}
```

首图解码后调用 `xTaskCreate(photo_font_task, "photo_font", 4096, NULL, 4, NULL)`；失败时先灰阶全刷，再在 app_main 同步调用 `load_photo_fonts()`。字体未 DONE 时 Back 有效，Confirm 显示“正在加载”，Left/Right 不读 SD；DONE 但无 READY 时显示“字体不可用”。

- [ ] **Step 5: 恢复完整交互和刷新策略**

默认 `view=PREVIEW`。PREVIEW Left/Right 从相邻 index 调用 `ink_photo_catalog_find_decodable()`，成功即整屏灰阶全刷；Confirm 在字体 READY 后绘制列表并执行 BW full refresh。LIST Left/Right 保持局刷并在 50 次成功局刷后全刷；Confirm 解码所选项并切回 PREVIEW；两个 view 的短按 Back 都直接调用 `ink_boot_switch_to_launcher()`。

加入 `APP_STAGE`：`sd_mounted`、`catalog_loaded`、`first_photo_decoded`、`font_start`、`font_done`、`first_refresh_done`。

- [ ] **Step 6: 测试并 build photo**

Run:

```powershell
python -m pytest tools/tests/test_app_startup_contracts.py -q
.\components\ink_photo_core\test\run_host_tests.ps1
. .\tools\idf_env.ps1
idf.py -C apps\photo build
```

Expected: pytest 和 host test PASS；photo `Project build complete`。

- [ ] **Step 7: 提交**

```powershell
git add apps/photo/main/app_main.c tools/tests/test_app_startup_contracts.py
git commit -m "相册：首屏显示图片并并行加载列表字体"
```

### Task 7: 全量构建、COM9 烧录与迁移记录

**Files:**
- Modify: `docs/MIGRATION_NOTES.md`

- [ ] **Step 1: 运行全部主机测试**

Run:

```powershell
python -m pytest tools/tests/test_generate_ui_text_bitmaps.py tools/tests/test_app_startup_contracts.py -q
.\components\ink_reader_core\test\run_host_tests.ps1
.\components\ink_photo_core\test\run_host_tests.ps1
```

Expected: 全部 PASS，无 warning/error。

- [ ] **Step 2: 使用固定 ESP-IDF 环境构建三个 app**

Run:

```powershell
$env:IDF_TOOLS_PATH = 'C:\Espressif'
$env:IDF_PATH = 'C:\esp\v5.5.4\esp-idf'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
.\tools\build_all.ps1
```

Expected: launcher、reader、photo 均 `Project build complete`。

- [ ] **Step 3: 依次烧录 COM9**

Run:

```powershell
.\tools\flash_launcher.ps1 -Port COM9
.\tools\flash_reader.ps1 -Port COM9
.\tools\flash_photo.ps1 -Port COM9
```

Expected: 三次 esptool 均完成校验；最后硬复位进入 launcher。

- [ ] **Step 4: 串口完成交互验收**

Run:

```powershell
idf.py -C apps\launcher -p COM9 monitor
```

按顺序执行 launcher -> reader -> launcher -> photo -> 列表 -> 图片 -> launcher。记录 `APP_STAGE` 数值，确认 launcher 无 `ink_sd: mounted`，reader 无 `ink_fonts`，photo 的 `font_start`/`font_done` 位于首次图片流程内。

日志必须不含：

```text
panic
assert failed
Guru Meditation
sdmmc_read_sectors: not enough mem
panel not ready after sw reset
epd busy_wait aborted
wifi
voice_note
I2S
ASR
USB MSC
```

- [ ] **Step 5: 更新迁移记录**

在 `docs/MIGRATION_NOTES.md` 记录三个 build 结果、COM9 烧录时间、实测阶段耗时、字体 WARN 消失、launcher 横线/字号、photo 默认预览与坏图跳过结果。不得填入未实际观察到的数据。

- [ ] **Step 6: 最终检查并提交**

Run:

```powershell
git diff --check
git status --short
```

Expected: 只有 `docs/MIGRATION_NOTES.md` 是本任务未提交修改。

```powershell
git add docs/MIGRATION_NOTES.md
git commit -m "验证：记录快速启动与旧版界面实机结果"
```
