# 局部刷新与相册列表实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 launcher 增加旧版横线选择与区域局刷，为 photo 增加旧版样式相册列表和 50 次局刷清屏策略，并让所有业务 app 短按 Back 返回 launcher。

**Architecture:** `ink_hw` 只新增同步区域局刷原语；`ink_epd_ui` 只负责页面绘制和区域计算；photo 的列表/预览状态与刷新计数保留在独立 photo 固件的 `app_main.c`。从旧项目只读取 GDEY0426T82 窗口换算、同步局刷命令和相册列表布局常量，不迁移 runtime、mailbox、interrupt、cancel 或协调器。

**Tech Stack:** ESP-IDF 5.5.4、ESP32-S3、FreeRTOS、GDEY0426T82 SPI、C、PowerShell、esptool、COM9。

---

## 文件结构

- 修改 `components/ink_hw/include/ink_hw.h`：公开同步区域局刷 API。
- 修改 `components/ink_hw/ink_hw.c`：实现区域对齐、portrait 到 native 区域转换、前后帧与局刷更新。
- 修改 `components/ink_epd_ui/include/ink_epd_ui.h`：公开 launcher/相册布局常量、区域类型和绘制接口。
- 修改 `components/ink_epd_ui/ink_epd_ui.c`：旧版横线选择、旧版相册列表布局和纯函数 self-test。
- 修改 `components/ink_photo_core/include/ink_photo_core.h`：为目录项保留可显示文件名。
- 修改 `components/ink_photo_core/ink_photo_core.c`：扫描时填充文件名，解析逻辑不变。
- 修改 `apps/launcher/main/app_main.c`：首次全刷，选择变化区域局刷并降级全刷。
- 修改 `apps/reader/main/app_main.c`：Back 按下边沿立即回 launcher。
- 修改 `apps/photo/main/app_main.c`：列表/预览状态机、50 次计数、短按 Back。
- 修改 `docs/MIGRATION_NOTES.md`：记录构建、烧录和实机结果。

### Task 1：Back 短按返回 launcher

**Files:**
- Modify: `apps/reader/main/app_main.c`
- Modify: `apps/photo/main/app_main.c`

- [ ] **Step 1: 写失败的静态行为检查**

运行：

```powershell
$files = 'apps/reader/main/app_main.c','apps/photo/main/app_main.c'
$held = Select-String -Path $files -Pattern 'ink_input_held_ms.*INK_BUTTON_BACK|>=\s*1200'
if ($held) { $held; throw 'Back 仍依赖长按' }
```

预期：FAIL，命中 reader 和 photo 的 `1200` 长按判断。

- [ ] **Step 2: 改为按下边沿**

两个 app 均删除 `back_latched` 和 held-time 条件，使用：

```c
if (ink_input_was_pressed(&input, INK_BUTTON_BACK)) {
  ESP_LOGI(TAG, "BOOT_SWITCH from=reader to=launcher");
  esp_err_t ret = ink_boot_switch_to_launcher();
  if (ret != ESP_OK)
    ESP_LOGE(TAG, "return to launcher failed err=%s", esp_err_to_name(ret));
}
```

photo 使用相同代码，但日志中的 `from=photo`。

- [ ] **Step 3: 验证静态行为和构建**

运行 Step 1 命令，预期 PASS；随后运行：

```powershell
& .\tools\build_all.ps1
```

预期：三个项目均输出 `Project build complete`，退出码 0。

- [ ] **Step 4: 提交**

```powershell
git add apps/reader/main/app_main.c apps/photo/main/app_main.c
git commit -m "交互：短按返回启动器"
```

### Task 2：同步区域局刷硬件原语

**Files:**
- Modify: `components/ink_hw/include/ink_hw.h`
- Modify: `components/ink_hw/ink_hw.c`
- Modify: `apps/launcher/main/app_main.c`

- [ ] **Step 1: 先接入尚不存在的 API，制造链接失败**

在 header 声明：

```c
esp_err_t ink_hw_partial_refresh_area(const uint8_t *buffer, size_t length,
                                      uint16_t x, uint16_t y,
                                      uint16_t width, uint16_t height);
```

在 launcher 首次全刷后添加一次默认不执行但会保留链接引用的检查：

```c
static volatile bool s_partial_link_check;

if (s_partial_link_check) {
  (void)ink_hw_partial_refresh_area(framebuffer, INK_EPD_BUFFER_SIZE,
                                    0, 0, 8, 8);
}
```

运行：

```powershell
. .\tools\idf_env.ps1
idf.py -C .\apps\launcher build
```

预期：FAIL，undefined reference to `ink_hw_partial_refresh_area`。

- [ ] **Step 2: 增加状态缓冲和参数校验**

在 `ink_hw.c` 增加上一帧 native 缓冲：

```c
static uint8_t *s_shadow;
```

在 `ink_hw_init()` 与 `s_native` 一起分配 48,000 bytes；任一分配失败返回 `ESP_ERR_NO_MEM`。全刷和灰阶全刷成功后将最终 native 平面复制到 `s_shadow`。

区域 API 必须拒绝空指针、短 buffer、零宽高和越界区域：

```c
if (!s_initialized || !buffer || length < INK_HW_BUFFER_SIZE ||
    width == 0 || height == 0 || x >= INK_HW_WIDTH || y >= INK_HW_HEIGHT ||
    (uint32_t)x + width > INK_HW_WIDTH ||
    (uint32_t)y + height > INK_HW_HEIGHT)
  return ESP_ERR_INVALID_ARG;
```

- [ ] **Step 3: 实现 portrait 区域到 native 区域换算**

按字节边界扩展 portrait x：

```c
uint16_t px0 = x & (uint16_t)~7u;
uint16_t px1 = (uint16_t)(((uint32_t)x + width + 7u) & ~7u);
if (px1 > INK_HW_WIDTH) px1 = INK_HW_WIDTH;
uint16_t native_x = y;
uint16_t native_y = INK_HW_WIDTH - px1;
uint16_t native_width = height;
uint16_t native_height = px1 - px0;
```

将 native x 起点向下对齐到 8、终点向上对齐到 8，并裁剪到 `800x480`。复用现有 `convert()` 的旋转关系，仅遍历扩展后的 portrait 区域，把结果写入 `s_native` 对应区域；区域之外保持不变。

- [ ] **Step 4: 实现同步局刷序列**

使用现有同步 helper 组成以下顺序：

```c
ESP_RETURN_ON_ERROR(init_sequence(false), TAG, "partial init");
ESP_RETURN_ON_ERROR(command(0x3c), TAG, "partial border cmd");
ESP_RETURN_ON_ERROR(data_byte(0x80), TAG, "partial border data");
ESP_RETURN_ON_ERROR(set_native_window(native_x, native_y,
                                      native_width, native_height),
                    TAG, "partial window");
ESP_RETURN_ON_ERROR(command(0x24), TAG, "partial current cmd");
ESP_RETURN_ON_ERROR(write_native_area(s_native, native_x, native_y,
                                      native_width, native_height),
                    TAG, "partial current data");
ESP_RETURN_ON_ERROR(command(0x26), TAG, "partial previous cmd");
ESP_RETURN_ON_ERROR(write_native_area(s_shadow, native_x, native_y,
                                      native_width, native_height),
                    TAG, "partial previous data");
ESP_RETURN_ON_ERROR(command(0x22), TAG, "partial update cmd");
ESP_RETURN_ON_ERROR(data_byte(0xff), TAG, "partial update mode");
ESP_RETURN_ON_ERROR(command(0x20), TAG, "partial activate");
ESP_RETURN_ON_ERROR(wait_ready("partial_update"), TAG, "partial timeout");
```

成功后只复制 `s_native` 对应区域到 `s_shadow`。不得加入 callback、phase 或 interrupt。

- [ ] **Step 5: 删除临时 link-check 并验证**

删除 `s_partial_link_check` 及其条件调用。运行：

```powershell
& .\tools\build_all.ps1
```

预期：三个项目构建通过；`rg -n "cancel|aborted|mailbox|interrupt" components/ink_hw` 不出现新接口。

- [ ] **Step 6: 提交**

```powershell
git add components/ink_hw/include/ink_hw.h components/ink_hw/ink_hw.c apps/launcher/main/app_main.c
git commit -m "驱动：增加同步区域局刷"
```

### Task 3：launcher 旧版横线选择与区域局刷

**Files:**
- Modify: `components/ink_epd_ui/include/ink_epd_ui.h`
- Modify: `components/ink_epd_ui/ink_epd_ui.c`
- Modify: `apps/launcher/main/app_main.c`

- [ ] **Step 1: 扩充 UI self-test，先观察失败**

公开并使用：

```c
typedef struct { int x; int y; int width; int height; } ink_epd_region_t;
ink_epd_region_t ink_epd_ui_launcher_selection_region(int previous,
                                                      int selected);
```

在 `ink_epd_ui_self_test()` 添加：

```c
ink_epd_region_t r = ink_epd_ui_launcher_selection_region(0, 1);
if (r.x != 92 || r.y != 292 || r.width != 28 || r.height != 164)
  return false;
```

先只添加声明和断言，运行 launcher build，预期 FAIL：函数未定义。

- [ ] **Step 2: 改 launcher 绘制样式**

保留标题、两行文字和 footer，删除黑底矩形。使用固定布局：

```c
static const int kLauncherRows[2] = {300, 440};
static const int kLauncherMarkerX = 96;
static const int kLauncherMarkerWidth = 20;
static const int kLauncherMarkerHeight = 4;
```

选中项绘制：

```c
ink_epd_ui_fill_rect(buffer, length, kLauncherMarkerX,
                     kLauncherRows[i] + 12,
                     kLauncherMarkerWidth,
                     kLauncherMarkerHeight, active);
ink_epd_ui_draw_text(buffer, length, 135, kLauncherRows[i], 4,
                     labels[i], true);
```

区域函数返回包含旧、新横线并留 4px 边距的联合矩形。上述 `0 -> 1` 应返回 `{92, 308, 28, 144}`；同步修正 self-test 期望为这组实际几何值。

- [ ] **Step 3: launcher 接入区域局刷与降级**

选择变化前保存 `previous`，重绘完整 framebuffer，然后：

```c
ink_epd_region_t region =
    ink_epd_ui_launcher_selection_region(previous, selected);
ret = ink_hw_partial_refresh_area(framebuffer, INK_EPD_BUFFER_SIZE,
                                  region.x, region.y,
                                  region.width, region.height);
if (ret != ESP_OK) {
  ESP_LOGE(TAG, "launcher partial refresh failed x=%d y=%d w=%d h=%d err=%s",
           region.x, region.y, region.width, region.height,
           esp_err_to_name(ret));
  ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
}
```

首次页面仍调用 `ink_hw_full_refresh()`。

- [ ] **Step 4: 构建并提交**

```powershell
. .\tools\idf_env.ps1
idf.py -C .\apps\launcher build
git add components/ink_epd_ui apps/launcher/main/app_main.c
git commit -m "界面：启动器使用横线局刷选择"
```

预期：launcher 构建通过，源码不再出现选中行黑底反白。

### Task 4：旧版样式相册列表纯 UI

**Files:**
- Modify: `components/ink_photo_core/include/ink_photo_core.h`
- Modify: `components/ink_photo_core/ink_photo_core.c`
- Modify: `components/ink_epd_ui/include/ink_epd_ui.h`
- Modify: `components/ink_epd_ui/ink_epd_ui.c`

- [ ] **Step 1: 为 catalog 增加显示名称并制造失败测试**

修改条目：

```c
typedef struct {
  char path[INK_PHOTO_PATH_MAX];
  char name[INK_PHOTO_PATH_MAX];
} ink_photo_item_t;
```

在 `ink_photo_core_self_test()` 增加纯函数测试，要求 `photo.bmp` 显示为 `photo`；先调用尚不存在的 `display_name()`，构建预期 FAIL。

- [ ] **Step 2: 实现文件名去扩展名**

实现受限复制：找到最后一个 `.`，复制其前内容；没有扩展名则复制完整文件名；结果始终 NUL 结尾。目录扫描时同时填充 `path` 和 `name`，排序仍按 path/name 的现有顺序保持确定性。

- [ ] **Step 3: 增加相册列表绘制接口**

为避免 UI 组件反向依赖 photo core，只公开轻量 row view：

```c
#define INK_PHOTO_LIST_ROW_HEIGHT 42
#define INK_PHOTO_LIST_ROW_GAP 2
#define INK_PHOTO_LIST_VISIBLE_ROWS 14
typedef struct { const char *name; } ink_epd_photo_row_t;
void ink_epd_ui_draw_photo_list(uint8_t *buffer, size_t length,
                                const ink_epd_photo_row_t *rows,
                                size_t count, size_t selected);
ink_epd_region_t ink_epd_ui_photo_list_selection_region(size_t previous,
                                                        size_t selected,
                                                        size_t count);
```

photo app 负责从 catalog 生成最多 64 个 row view。

- [ ] **Step 4: 按旧版常量绘制列表**

采用旧版精确常量：list x=24、list y=50、row width=432、row height=42、gap=2、visible rows=14、content x inset=16、marker top/bottom inset=8、title y offset=8。页面 header 为 `PHOTO ALBUM`，条目标题格式为两位序号加文件名，选中行左侧绘制 4px marker，并在选中行右侧绘制 `current/total`。列表滚动窗口让 selected 尽量位于中部，接近尾部时贴齐最后 14 项。

区域函数比较 previous 与 selected 对应的可见窗口：窗口未滚动时返回两行联合区域；窗口变化时返回整个列表区域，以免旧条目残留。

- [ ] **Step 5: self-test 与构建**

在 `ink_epd_ui_self_test()` 验证：0->1 返回两行区域；7->8 导致窗口滚动并返回全列表区域；空列表绘制 `NO PHOTOS FOUND`。运行：

```powershell
. .\tools\idf_env.ps1
idf.py -C .\apps\photo build
```

预期：photo 构建通过且 self-test 返回 true。

- [ ] **Step 6: 提交**

```powershell
git add components/ink_photo_core components/ink_epd_ui
git commit -m "界面：增加旧版样式相册列表"
```

### Task 5：photo 列表/预览状态机与 50 次清屏

**Files:**
- Modify: `apps/photo/main/app_main.c`

- [ ] **Step 1: 定义纯刷新策略并写失败断言**

增加：

```c
enum { PHOTO_PARTIAL_REFRESH_LIMIT = 50 };
typedef enum { PHOTO_VIEW_LIST, PHOTO_VIEW_PREVIEW } photo_view_t;
static bool photo_should_full_refresh(unsigned successful_partial_count) {
  return successful_partial_count >= PHOTO_PARTIAL_REFRESH_LIMIT;
}
```

先在启动 self-test 中引用尚不存在的 helper：49 返回 false、50 返回 true，构建预期 FAIL；再实现上面的最小函数使其通过。

- [ ] **Step 2: 启动进入列表**

保留 SD mount 和 catalog load，构造 `ink_epd_photo_row_t rows[INK_PHOTO_MAX_ITEMS]`。初始状态：

```c
photo_view_t view = PHOTO_VIEW_LIST;
size_t current = 0;
unsigned successful_partial_count = 0;
```

绘制列表或空目录状态，并执行一次 `ink_hw_full_refresh()`。

- [ ] **Step 3: 列表导航采用局刷/第 51 次全刷**

Left/Right 更新 current 后重绘 framebuffer。若 `photo_should_full_refresh(successful_partial_count)` 为 true，执行全刷并清零；否则执行区域局刷。成功局刷后计数加一；局刷失败立即全刷，成功后计数清零。

这意味着第 1 至 50 次选择变化执行局刷，第 51 次选择变化执行清屏全刷，符合“累计 50 次后下一次全刷”的设计。

- [ ] **Step 4: Confirm 切换列表与预览**

列表中 Confirm：解码当前 BMP，成功后切换 `PHOTO_VIEW_PREVIEW` 并调用现有 `ink_hw_gray_refresh()`；失败则保留列表并显示错误。预览中 Confirm：切回 `PHOTO_VIEW_LIST`，重绘列表并执行一次全刷，计数清零。

预览中的 Left/Right 可继续切换图片，每张图片都调用四灰阶全刷；Back 在任一 view 都执行 Task 1 的短按返回。

- [ ] **Step 5: 构建与静态检查**

```powershell
& .\tools\build_all.ps1
rg -n "PHOTO_PARTIAL_REFRESH_LIMIT = 50|PHOTO_VIEW_LIST|PHOTO_VIEW_PREVIEW" apps/photo/main/app_main.c
```

预期：三个项目通过，三个状态/阈值均存在；禁止模块扫描无命中。

- [ ] **Step 6: 提交**

```powershell
git add apps/photo/main/app_main.c
git commit -m "功能：实现相册列表与定期清屏"
```

### Task 6：文档、完整构建和 COM9 验收

**Files:**
- Modify: `docs/MIGRATION_NOTES.md`

- [ ] **Step 1: 完整构建与排除项扫描**

```powershell
& .\tools\build_all.ps1
$patterns='voice_note|\bASR\b|i2s_|esp_wifi|tinyusb|usb_msc|ink_resource_coordinator|ink_background_flush|runtime_shell|display_mailbox'
rg -n -i $patterns apps components --glob '*.[ch]' --glob 'CMakeLists.txt'
```

预期：build 退出码 0；`rg` 退出码 1（无命中）。

- [ ] **Step 2: 烧录三个镜像**

```powershell
& .\tools\flash_launcher.ps1 -Port COM9
& .\tools\flash_reader.ps1 -Port COM9
& .\tools\flash_photo.ps1 -Port COM9
```

每条命令预期包含 `Hash of data verified` 和 `Hard resetting via RTS pin`，退出码 0。

- [ ] **Step 3: 实机交互验收**

monitor launcher 并依次验证：

```powershell
. .\tools\idf_env.ps1
idf.py -C .\apps\launcher -p COM9 monitor
```

检查 launcher 横线局刷、reader Back 短按、photo 列表局刷、Confirm 打开照片全刷、Confirm 返回列表、photo Back 短按，以及第 51 次列表选择变化的自动全刷。

- [ ] **Step 4: 更新迁移记录**

在 `docs/MIGRATION_NOTES.md` 追加实际镜像大小、烧录地址、串口日志、屏幕表现和未通过项。不得把未操作或未观察的项目写成 PASS。

- [ ] **Step 5: 最终检查并提交**

```powershell
git diff --check
git status --short
git add docs/MIGRATION_NOTES.md
git commit -m "验证：记录局刷与相册实机结果"
```

预期：提交后工作树干净。实机全部通过后才进入合并决策。
