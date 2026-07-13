# 旧版 UI、SD 字体与照片全刷对齐设计

## 目标

在保持 launcher、reader、photo 三个独立 ESP-IDF 固件和共享小组件架构的前提下，将第一阶段可见 UI、SD 目录、字体渲染和照片预览刷新行为与旧版固件对齐。只迁移同步硬件/字体/绘制纯能力，不恢复旧 runtime、display mailbox、后台协调器或多 app 同进程模型。

## 已确认根因

- SDMMC 已能挂载 `/sdcard`；此前串口已打印 `ink_sd: mounted path=/sdcard`，问题不是 SD mount 缺失。
- reader 当前只扫描 `/sdcard/books` 与 `/sdcard` 根目录中的 `.xtc/.xtch`。目录为空、扩展名不同或文件解析失败都会表现为未找到书籍。
- 当前 `ink_fonts` 只探测 cpfont 文件头，不加载字形；`ink_epd_ui` 仅有 ASCII 5x7 字体。中文 UTF-8 字节会被逐字节映射为 `?`，这是 photo 中文乱码的直接原因。
- photo 预览调用四灰阶接口，但精简驱动的灰阶初始化与旧版仍有差异，缺少旧版灰阶路径中的 `0x18/0x80` 温度设置。需要按旧版完整同步序列对齐并增加明确日志。
- 当前 launcher 只迁移了选择横线，没有迁移旧版 header、卡片、图标、箭头与文字布局。

## SD 目录与格式

三个 app 沿用旧版目录：

```text
/sdcard/books/*.xtc 或 *.xtch
/sdcard/photos/*.bmp
/sdcard/fonts/*.cpfont
/sdcard/.fonts/**/*.cpfont
```

第一阶段不增加 TXT、EPUB、JPEG 或 PNG。reader 继续读取 XTC/XTCH 中的 XTG/XTH 预渲染页面；photo 继续读取现有 480x800、4bpp indexed BMP。

## 字体组件

`ink_fonts` 从旧版 `ink_cpfont` 提取以下同步能力：

- cpfont 文件加载、关闭和 loaded 状态。
- UTF-8 codepoint 解码。
- interval/glyph 查找与 bitmap 读取。
- 单色 framebuffer 文字绘制、缩放绘制和宽度测量。
- 小型字形缓存可以保留在组件内部，但不允许依赖 SDIO job、resource coordinator、runtime service 或后台任务。

组件优先加载旧版 menu/footer/reader 候选路径，并在固定候选失败后扫描 `/sdcard/fonts` 与 `/sdcard/.fonts`。所有文件读取均在当前 app 内同步完成。

字体缺失时允许英文 ASCII 错误页继续工作，并逐项记录搜索结果；中文 UTF-8 不允许静默回退为乱码。

## launcher

launcher 允许挂载 SD，但只用于加载 menu/footer 字体，不扫描书籍或图片。

页面使用旧版 crosspoint 布局参数：

- header gutter 及 divider 沿用旧版。
- card/list x=24、width=432。
- row height=70、gap=6。
- 只显示 Reader 和 Photo，并占用旧六卡列表的前两行位置。
- Reader 使用旧版 book icon，Photo 使用旧版 photo icon。
- 每行保留旧版标题、说明文字和右侧 chevron。
- 选中状态继续采用用户指定的文字左侧横线，不使用黑底反白。

首次进入 launcher 执行全刷；选择变化只局刷旧、新横线覆盖的区域。字体加载失败时显示英文 Reader/Photo，但卡片、图标和几何保持不变。

## reader

reader 启动顺序为 EPD、按键、SD、字体、书籍扫描。扫描结果需要区分并打印：

- SD mount 失败。
- `/sdcard/books` 不存在或无法打开。
- 目录存在但没有 `.xtc/.xtch`。
- 找到候选文件但 header/page index 校验失败。
- 成功打开书籍及页数。

reader 状态页和菜单文字使用 cpfont；书页仍直接显示 XTG/XTH 预渲染 framebuffer，不重新排版。Back 短按返回 launcher 保持不变。

## photo

### 相册列表

photo 启动后仍默认进入相册列表。列表沿用已确认的旧版参数：x=24、y=50、width=432、row height=42、gap=2、14 个可见条目。标题、文件名、计数和状态文字使用 cpfont，中文文件名按 UTF-8 codepoint 截断和测量，不按字节截断。

列表导航继续使用区域局刷，累计 50 次成功局刷后下一次执行全刷。Confirm 打开选中照片，Back 短按返回 launcher。

### 照片预览

照片预览只允许调用完整四灰阶刷新路径，不得调用区域局刷。同步序列与旧版一致：

1. panel reset、BUSY 等待、software reset。
2. boost/gate/border 初始化。
3. `0x18` 写入 `0x80`。
4. 设置完整 800x480 native window。
5. 加载旧版四灰阶 LUT。
6. `0x26` 写入 MSB plane，`0x24` 写入 LSB plane。
7. `0x22` 写入 `0xC7`，`0x20` 激活并等待完成。

每次列表进入预览或预览左右切图都执行该完整路径，并打印 `PHOTO_REFRESH mode=full_gray`。预览 Confirm 返回列表并全刷列表；Back 在列表和预览中均返回 launcher。

## 组件边界

- `ink_sd`：仅负责同步挂载，不缓存目录或业务数据。
- `ink_fonts`：只负责 cpfont 加载与绘制。
- `ink_epd_ui`：接受可选字体对象，绘制 launcher、列表和状态页；不直接挂载 SD。
- `ink_reader_core`：继续负责 XTC/XTCH 扫描、校验与页读取，并返回可诊断结果。
- `ink_photo_core`：继续负责 BMP catalog 和解析。
- `ink_hw`：提供单色全刷、区域局刷和完整四灰阶全刷，不感知 app 页面。
- 各 app 在自己的 `app_main.c` 中按顺序初始化和持有资源，运行时互不共存。

## 错误处理

- 字体加载失败：记录候选路径，改用英文 ASCII 状态，不输出乱码。
- SD/目录错误：显示明确错误页，Back 始终可用。
- reader 候选文件解析失败：继续扫描其余候选；全部失败后显示 `BOOK FORMAT ERROR`，与空目录区分。
- photo 四灰阶全刷失败：保留当前 view/index 的已提交状态，显示 `IMAGE ERROR`，下一次操作不得降级为局刷预览。

## 验证

### 自动验证

- cpfont UTF-8 解码、glyph 查找、字宽、中文绘制和缺字行为。
- launcher 卡片几何、book/photo icon、chevron、横线与局刷区域。
- reader 对目录缺失、空目录、无效 XTC 和有效 XTC 的结果分类。
- photo 中文文件名按 codepoint 截断，不产生破损 UTF-8。
- gray refresh 命令顺序包含 `0x18/0x80`、完整 window、双 plane 和 `0xC7`。
- 三项目通过 `tools/build_all.ps1`，禁止模块扫描无命中。

### COM9 实机验证

- launcher 中文 header、两张旧版卡片、图标、箭头与横线选择显示正确。
- launcher 只为字体挂载 SD，无 WiFi/语音/USB 日志。
- reader 能发现 `/sdcard/books` 下有效 XTC/XTCH，或准确报告空/格式错误。
- photo 中文文件名正常显示，无问号乱码。
- 打开和切换照片时串口打印 `PHOTO_REFRESH mode=full_gray`，屏幕执行完整四灰阶全屏刷新。
- 三个 app Back 短按均能返回 launcher。
- 日志无 panic、assert failed、Guru Meditation、SD 内存不足、EPD reset/busy abort 或禁止模块内容。
