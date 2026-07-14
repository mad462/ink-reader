# App 切换加载提示 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在四条 App 切换路径中先用快速局刷显示 Flash 内置的 `Now Loading...` 图片，再切换 boot partition 并重启。

**Architecture:** `ink_epd_ui` 保存由 `素材/now_loading.bmp` 机械转换的 200x40 1bpp 像素，并提供绘制函数与固定局刷区域。Launcher、Reader、Photo 保持对切换时序的所有权：绘制、局刷、记录结果，然后调用现有 `ink_boot_switch_to_*()`；加载局刷失败不阻止重启。

**Tech Stack:** ESP-IDF 5.5.4、C、1bpp framebuffer、TinyCC host test、pytest source contract、PowerShell、COM9。

---

### Task 1: Flash 内置加载图片与 UI 契约

**Files:**
- Create: `components/ink_epd_ui/ink_loading_asset.c`
- Create: `components/ink_epd_ui/ink_loading_asset.h`
- Modify: `components/ink_epd_ui/CMakeLists.txt`
- Modify: `components/ink_epd_ui/include/ink_epd_ui.h`
- Modify: `components/ink_epd_ui/test/reader_ui_host_test.c`
- Modify: `components/ink_epd_ui/test/run_host_tests.ps1`

- [ ] **Step 1: 写失败测试**

在 host test 中断言 `ink_epd_ui_loading_region()` 等于 `(132,372,216,56)`；在全黑 framebuffer 上调用 `ink_epd_ui_draw_loading()` 后，区域的 8px 外围为白色，中心 200x40 图像与源 BMP 的黑白像素指纹一致，区域外像素不变。

- [ ] **Step 2: 验证 RED**

Run: `components/ink_epd_ui/test/run_host_tests.ps1`

Expected: 因加载 UI API 尚不存在而编译失败。

- [ ] **Step 3: 生成并实现最小资源代码**

从 `D:\FUCKIDF\ink-reader-v2\素材\now_loading.bmp` 读取 200x40、1bpp、底向上 BMP，按调色板归一化为 `1=black` 的 25 bytes/row C 数组。实现：

```c
ink_epd_region_t ink_epd_ui_loading_region(void);
void ink_epd_ui_draw_loading(uint8_t *buffer, size_t length);
```

绘制时先将 `(132,372,216,56)` 清白，再把图片画到 `(140,380)`；不得读取 SD、文件系统或 cpfont。

- [ ] **Step 4: 验证 GREEN**

Run: `components/ink_epd_ui/test/run_host_tests.ps1`

Expected: `PASS: reader UI host self test`。

### Task 2: 四条切换路径接入局刷

**Files:**
- Modify: `apps/launcher/main/app_main.c`
- Modify: `apps/reader/main/app_main.c`
- Modify: `apps/photo/main/app_main.c`
- Modify: `tools/tests/test_app_startup_contracts.py`

- [ ] **Step 1: 写失败契约测试**

断言 Launcher 到 Reader/Photo、Reader 到 Launcher（正常书库和内存异常等待路径）、Photo 到 Launcher 都在对应 `BOOT_SWITCH` 与 `ink_boot_switch_to_*()` 之前调用加载绘制和 `ink_hw_partial_refresh_area()`；三 App 均包含 `BOOT_LOADING from=%s to=%s refresh=%s` 日志。

- [ ] **Step 2: 验证 RED**

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k boot_loading -q`

Expected: 新契约测试失败。

- [ ] **Step 3: 实现 App 本地薄包装**

每个 App 添加本地 `show_boot_loading()`：调用共享 UI、获取固定区域、打印 `BOOT_LOADING`、执行一次快速区域局刷并记录 `ok/failed`。调用方随后打印现有 `BOOT_SWITCH` 并调用原 boot switch；刷新失败只记警告，不改变切换行为。

- [ ] **Step 4: 验证 GREEN**

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k boot_loading -q`

Expected: 新契约测试通过。

### Task 3: 审查、完整构建和实机验证

**Files:**
- No production file additions beyond Tasks 1-2.

- [ ] **Step 1: 一次代码审查**

检查像素极性、BMP 行方向、局刷区域边界、四条切换时序、失败后继续重启，以及未引入 SD/字体/runtime 依赖。发现问题交回同一实现代理修复。

- [ ] **Step 2: 最终验证**

Run: `components/ink_epd_ui/test/run_host_tests.ps1`

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -k "boot_loading or launcher_has_no_sd" -q`

Run: `tools/build_all.ps1`

Expected: host test、相关 pytest、三个 ESP-IDF App 全部通过。

- [ ] **Step 3: COM9 一轮三镜像验证**

依次把 Launcher、Reader、Photo 写入既定分区，不启动后台 monitor。人工验证四条切换均先出现居中加载图片，随后进入目标 App；确认后再统一中文提交当前未提交改动。
