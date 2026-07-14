# Reader 旧版功能对齐设计

## 目标

在独立 Reader 固件中恢复旧版书库和阅读交互，同时保持 v2 的单 app 独占 EPD/SD/按键架构。不得引入旧 runtime、app registry、display mailbox、resource coordinator 或后台跨 app 服务。

## 页面与返回层级

Reader 启动后进入书库，默认标签为“最近”。页面层级固定为：

`书库 -> 书目操作弹窗 -> 阅读页 -> 章节/书签菜单`

- 阅读页短按 Back：保存进度并返回书库。
- 书库条目焦点短按 Back：把焦点移到标签。
- 书库标签焦点短按 Back：切换到 launcher 分区。
- Confirm 在书库打开书目操作弹窗，在阅读页打开章节/书签菜单。

## Core 数据

扩展 `ink_reader_core`，不新增运行时框架：

- XTC/XTCH：保留现有页索引和位图解码，补齐 metadata、章节索引、章节边界校验。
- Catalog：扫描 `/sdcard/books`，最多 32 本，按文件名不区分大小写排序。
- State：兼容旧 `/sdcard/.ink-reader/state.bin`，magic `0x49534150`、version 4，并兼容 v1-v3 读取。
- State 容量：32 本书架记录、16 条进度、12 个书签。
- 最近：仅 `has_opened` 的书，按 `recent_order` 降序。
- 收藏：收藏状态立即持久化；取消收藏后从收藏标签立即移除。
- 普通翻页只更新 RAM；返回书库、退出 Reader、收藏和书签操作时保存。
- 新保存采用同目录临时文件再 rename，读取继续兼容旧裸结构文件。

## 书库 UI

标签顺序为“最近 / 全部 / 收藏”，保留旧版几何：

- 页面区域 `(8,8,464,776)`；标签 `x=24,y=58,w=138,h=42,gap=8`。
- 书卡 `x=24,y=108,w=432,h=58,gap=6`，最多显示 8 项。
- 卡片显示书名、页码/总页数、章节和收藏标记。
- 操作弹窗 `(54,314,372,164)`，动作是“打开”和动态“加入收藏/取消收藏”。
- 标签、条目和弹窗选择均使用局刷；打开书和返回书库使用全刷。

## 阅读菜单与 Footer

- 菜单标签为“章节 / 书签”，状态层级为 `TABS / ITEMS / BOOKMARK_ACTIONS`。
- 菜单 panel `(24,118,432,534)`；标签 `y=136,h=42,gap=10`。
- 章节卡 `y=200,h=48,gap=8`；书签卡 `y=200,h=58,gap=8`。
- 章节窗口最多显示 8 项；书签窗口最多显示 6 项并随选择滚动，修正旧版第 7/8 张书签卡越过 panel 的布局缺陷。
- 章节确认跳到 `start_page` 并关闭菜单。
- 书签第 0 项是“将当前页添加到书签”。已有书签支持“跳转 / 覆盖 / 删除”。
- 书签时间沿用旧逻辑 `T+HH:MM`，由页码生成，不依赖 WiFi 或系统时钟。
- Footer 区域 `(0,780,480,20)`，分隔线 `y=779`；左侧章节名，右侧 `%u%% %u/%u`。

## 刷新与错误处理

- 普通翻页继续使用现有 framebuffer 差异区域局刷，第 50 次维护性全刷。
- 书库和菜单先在内存中绘制候选帧，刷新成功才提交页面、焦点、页码和书签状态。
- 菜单关闭后重新渲染当前阅读页，防止 overlay 残留；任何卡片不得越过 panel 或 footer。
- 状态文件损坏时使用默认状态，不影响书籍扫描；保存失败保留 RAM 状态并记录错误。
- 章节或书签目标越界时不改变当前页。

## 禁止依赖

Reader 的依赖闭包只允许 `ink_reader_core`、`ink_epd_ui`、`ink_fonts`、`ink_hw`、`ink_input`、`ink_sd`、`ink_boot_switch`。源码和 CMake 中不得出现 `ink_system_runtime`、`ink_runtime_shell`、`ink_display_mailbox`、`ink_display_request`、`ink_system_services`、`ink_resource_coordinator` 或旧 app descriptor。

## 验收

- 启动进入旧版书库，最近/全部/收藏可切换。
- 收藏和取消收藏弹窗可用，重启 Reader 后状态保留。
- 打开书后恢复进度；Back 回书库，再从标签焦点 Back 回 launcher。
- 章节可跳转；书签可新增、跳转、覆盖、删除并持久化。
- Footer 显示章节、百分比和页码。
- Reader、Launcher、Photo 全部构建通过，串口无禁止日志。
