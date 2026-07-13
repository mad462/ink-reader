# Reader 快刷、相册启动与启动器字号实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消除 Reader 普通翻页反白，减少 Photo 首图前的重复 SD I/O，并恢复 Launcher 旧版应用名字号。

**Architecture:** Reader 在 app 内保存上一帧并计算差异矩形，继续复用小型 `ink_hw` 区域局刷接口；Photo catalog 只负责扩展名收集与排序；Launcher 继续使用 flash 固化短语。不会引入旧 runtime、mailbox、协调器或后台目录任务。

**Tech Stack:** ESP-IDF 5.5.4、C、FreeRTOS、Python pytest、PowerShell host tests、COM9。

---

### Task 1: Reader 差异区域快刷

**Files:**
- Modify: `apps/reader/main/app_main.c`
- Test: `tools/tests/test_app_startup_contracts.py`

- [ ] **Step 1: 写失败契约测试**

断言首刷调用 `ink_hw_full_refresh`，翻页块调用 `ink_hw_partial_refresh_area`，并存在 50 次清理阈值、刷新失败回滚和 `PAGE_REFRESH` 日志。

- [ ] **Step 2: 运行 RED**

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -q`
Expected: FAIL，当前翻页仍调用 `ink_hw_full_refresh`。

- [ ] **Step 3: 最小实现**

在 Reader PSRAM 中增加 48KB 上一帧缓冲；加入纯差异矩形计算和自检；前 49 次成功翻页调用区域局刷，第 50 次调用维护性全刷；失败时恢复 framebuffer 与 `book.current_page`。

- [ ] **Step 4: 运行 GREEN 与构建**

Run: `python -m pytest tools/tests/test_app_startup_contracts.py -q`
Run: `components/ink_reader_core/test/run_host_tests.ps1`
Run: `tools/build_all.ps1 reader`
Expected: 全部 PASS，reader build 成功。

### Task 2: Photo 目录扫描去重

**Files:**
- Modify: `components/ink_photo_core/ink_photo_core.c`
- Modify: `components/ink_photo_core/test/photo_palette_host_test.c`

- [ ] **Step 1: 写失败 host test**

构造包含扩展名合法但内容损坏 BMP 的临时目录，断言 catalog 仍收录候选且排序不变，实际解码阶段再报告失败。

- [ ] **Step 2: 运行 RED**

Run: `components/ink_photo_core/test/run_host_tests.ps1`
Expected: FAIL，当前 catalog 会预检并过滤损坏 BMP。

- [ ] **Step 3: 最小实现**

从 `ink_photo_catalog_load()` 删除 `probe_bmp_file()` 调用，只保留扩展名、路径长度、名称复制、上限和排序。保留公开 probe API 给独立验证使用。

- [ ] **Step 4: 运行 GREEN 与构建**

Run: `components/ink_photo_core/test/run_host_tests.ps1`
Run: `python -m pytest tools/tests -q`
Run: `tools/build_all.ps1 photo`
Expected: 全部 PASS，photo build 成功。

### Task 3: Launcher 24px 应用名

**Files:**
- Modify: `tools/generate_ui_text_bitmaps.py`
- Modify: `tools/tests/test_generate_ui_text_bitmaps.py`
- Regenerate: `components/ink_epd_ui/ink_ui_text_assets.c`
- Modify: `components/ink_epd_ui/ink_epd_ui.c`
- Modify: `docs/MIGRATION_NOTES.md`

- [ ] **Step 1: 写失败资产/UI 测试**

断言短语清单包含 24px“书库/相册”，生成资产宽高覆盖旧版约 `48x23` 的标题区域；源码不得再请求 12px launcher 标题。

- [ ] **Step 2: 运行 RED**

Run: `python -m pytest tools/tests/test_generate_ui_text_bitmaps.py -q`
Expected: FAIL，当前清单只有 12px 资产。

- [ ] **Step 3: 最小实现并重新生成**

将两条 launcher 标题改为 24px，生成 C 资产；在 70px 卡片内调整标题/说明 y 坐标并收紧 UI 自检。

- [ ] **Step 4: 全量验证、烧录与记录**

Run: `python -m pytest tools/tests -q`
Run: reader/photo host tests
Run: `tools/build_all.ps1`
Run: `tools/flash_launcher.ps1 COM9`、`tools/flash_reader.ps1 COM9`、`tools/flash_photo.ps1 COM9`
Expected: 三张镜像 build/flash 成功；COM9 实测 Reader 快刷、Photo 启动阶段和 Launcher 24px 标题；将实际耗时和观察结果写入 `docs/MIGRATION_NOTES.md`。
