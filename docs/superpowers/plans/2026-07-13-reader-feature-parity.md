# Reader 旧版功能对齐 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 v2 独立 Reader 固件中恢复旧版书库、收藏、最近、章节、书签、阅读菜单和 footer。

**Architecture:** 纯数据与解析进入 `ink_reader_core`，像素绘制进入 `ink_epd_ui`，页面/焦点/按键状态机留在 Reader app。保留 v2 直接刷新和分区切换，不迁移旧 runtime 或 mailbox。

**Tech Stack:** ESP-IDF 5.5.4、C、FreeRTOS、FATFS、GDEY0426T82、host C tests、pytest contract tests。

---

### Task 1: XTC metadata 与章节

**Files:**
- Modify: `components/ink_reader_core/include/ink_reader_core.h`
- Modify: `components/ink_reader_core/ink_reader_core.c`
- Modify: `components/ink_reader_core/test/reader_core_host_test.c`

- [ ] 先添加 host 用例，构造含 metadata、page index、chapter index 的 XTC，并断言章节数量、标题、`start_page` 和 page-to-chapter 映射；损坏 offset 必须打开失败。
- [ ] 运行 `components/ink_reader_core/test/run_host_tests.ps1`，确认新用例因章节 API 不存在而失败。
- [ ] 扩展 `ink_reader_book_t`，增加 `ink_reader_chapter_t` 数组、书名、章节查询和 `ink_reader_book_jump_to_chapter()`；close 时释放章节数组。
- [ ] 重新运行 host tests 与 `idf.py -C apps/reader build`。
- [ ] 提交：`书库：迁移 XTC 章节解析`

### Task 2: Catalog 与旧状态文件

**Files:**
- Create: `components/ink_reader_core/include/ink_reader_state.h`
- Create: `components/ink_reader_core/ink_reader_state.c`
- Modify: `components/ink_reader_core/include/ink_reader_core.h`
- Modify: `components/ink_reader_core/ink_reader_core.c`
- Modify: `components/ink_reader_core/CMakeLists.txt`
- Modify: `components/ink_reader_core/test/reader_core_host_test.c`

- [ ] 添加 catalog 排序、最近排序、收藏过滤、进度恢复、12 个书签和旧 v4 fixture 读取测试。
- [ ] 运行 host tests，确认 catalog/state API 缺失导致失败。
- [ ] 暴露 `ink_reader_catalog_load/free/count/at`；复用现有候选收集，限制 32 本。
- [ ] 从旧 `ink_app_state` 净化迁移到 `ink_reader_state`，保持旧结构布局和 v1-v4 读取；保存写 `.tmp` 后 rename。
- [ ] 运行 host tests、pytest 与 Reader build。
- [ ] 提交：`书库：迁移最近收藏与书签状态`

### Task 3: 旧版书库 UI

**Files:**
- Create: `components/ink_epd_ui/ink_reader_ui.c`
- Modify: `components/ink_epd_ui/include/ink_epd_ui.h`
- Modify: `components/ink_epd_ui/CMakeLists.txt`
- Modify: `components/ink_epd_ui/ink_epd_ui.c`
- Modify: `tools/tests/test_app_startup_contracts.py`

- [ ] 添加 UI 契约和 self-test，覆盖三标签、8 张书卡、收藏标记、空标签和两动作弹窗边界。
- [ ] 运行 pytest，确认新绘制 API 缺失导致失败。
- [ ] 新增纯 view 结构和 `ink_epd_ui_draw_library()`；用现有字体/primitive API 按设计中的旧几何绘制。
- [ ] 增加 `ink_epd_ui_library_selection_region()`，保证局刷区域覆盖旧/新焦点。
- [ ] 运行 pytest、Reader build。
- [ ] 提交：`书库：恢复旧版书库界面`

### Task 4: 书库状态机与操作弹窗

**Files:**
- Create: `apps/reader/main/reader_app_model.h`
- Create: `apps/reader/main/reader_app_model.c`
- Modify: `apps/reader/main/CMakeLists.txt`
- Modify: `apps/reader/main/app_main.c`
- Modify: `tools/tests/test_app_startup_contracts.py`

- [ ] 添加纯 model self-test：最近/全部/收藏切换、条目循环、Back 进入标签、popup 左右选择、取消收藏后索引收敛。
- [ ] 运行 Reader build，确认 model API 缺失。
- [ ] 实现 `LIBRARY/READING` 页面和 `ITEMS/TABS/POPUP` 焦点 reducer；app_main 只负责输入、绘制、刷新和持久化副作用。
- [ ] Reader 启动加载 catalog/state 并全刷书库，不再自动打开第一本书。
- [ ] 打开/收藏成功才提交候选状态；标签/选择走局刷。
- [ ] 运行 tests 与 Reader build。
- [ ] 提交：`书库：接入分类与收藏操作`

### Task 5: 章节、书签与阅读菜单

**Files:**
- Modify: `components/ink_epd_ui/ink_reader_ui.c`
- Modify: `components/ink_epd_ui/include/ink_epd_ui.h`
- Modify: `apps/reader/main/reader_app_model.h`
- Modify: `apps/reader/main/reader_app_model.c`
- Modify: `apps/reader/main/app_main.c`

- [ ] 添加 model self-test：Confirm 开菜单、tab 切换、章节跳页、当前页新增书签、已有书签跳转/覆盖/删除、Back 逐层退出。
- [ ] 添加 UI self-test：章节最多显示 8 项、书签最多显示 6 项并滚动，三动作弹窗在 480x800 内。
- [ ] 实现 `ink_epd_ui_draw_reader_menu()` 与菜单局刷区域。
- [ ] 连接章节和 state API；所有书签修改立即保存，失败保持原 model。
- [ ] 菜单关闭时重绘当前页；普通翻页继续使用差异局刷与第 50 次清理全刷。
- [ ] 运行 tests 与 Reader build。
- [ ] 提交：`阅读器：恢复章节与书签任务栏`

### Task 6: Footer、返回与最终验证

**Files:**
- Modify: `components/ink_epd_ui/ink_reader_ui.c`
- Modify: `apps/reader/main/app_main.c`
- Modify: `tools/tests/test_app_startup_contracts.py`
- Modify: `docs/MIGRATION_NOTES.md`

- [ ] 添加 footer 契约：左章节名，右 `%u%% %u/%u`，band y=780、separator y=779。
- [ ] 在每次页面解码后叠加 footer；保存时使用 display chapter 过滤规则。
- [ ] 实现两级 Back：READING 回 LIBRARY，LIBRARY/TABS 回 launcher，并在两处持久化进度。
- [ ] 运行 host tests、`python -m pytest tools/tests -q` 和 `tools/build_all.ps1`。
- [ ] 扫描 Reader 源码/CMake，确认无禁止模块符号。
- [ ] 烧录 COM9，验证书库、收藏、章节、书签、footer、返回和禁止日志。
- [ ] 更新一次迁移记录并提交：`阅读器：完成旧版功能对齐`
