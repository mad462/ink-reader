# 快速应用启动与旧版字号 UI 设计

## 背景

当前三固件架构的 app 切换本身没有异常。实机日志显示，从重启到 `APP_START name=reader` 约 0.85 秒；主要额外等待来自 SD 字体加载和 EPD 全刷。reader 依次加载 24px 菜单字体和 18px 阅读字体，每套约 1.2 秒。不存在的首选字体路径还会先打印 `font load failed`，随后备用路径实际加载成功，因此用户看到的是候选路径探测噪声，不是最终字体加载失败。

当前 launcher 与 photo 把 24px 菜单字体以 `scale_divisor=1` 绘制；旧固件的列表行标题和图片名使用 `scale_divisor=2`。这导致 v2 的卡片标题和图片名比旧版大约一倍。当前 launcher 选中横线位于图标和文字之间，也没有在 70px 卡片内垂直居中。

## 目标

- launcher 不依赖 SD 字体即可显示中文，并取消 launcher 的 SD 挂载。
- reader 不为预渲染书页加载无用的菜单字体，并消除不存在候选路径产生的 WARN。
- photo 启动后直接显示第一张可解码图片，把 SD 字体加载时间隐藏在图片灰阶全刷期间。
- launcher、reader 状态页、photo 列表和图片名恢复旧固件的字号比例与列表几何。
- 保持 launcher、reader、photo 三个独立固件，不引入多 app runtime 或资源协调器。

## 非目标

- 不改变分区布局、OTA boot partition 切换方式或 ESP32-S3 正常冷启动流程。
- 不加入 WiFi、USB、语音、I2S、ASR 或跨 app 常驻任务。
- 不在本阶段实现完整 reader 书库浏览器。
- 不把完整中文字库重复嵌入三个 app 固件。

## 设计

### 1. 固化的 UI 字形子集

从旧项目使用的 `LXGWWenKai-Regular.ttf` 生成只包含固定 UI 文案的单色字形子集，作为 `ink_epd_ui` 的只读 flash 数据。保留两种视觉尺寸：24px 用于页面标题，16px 用于固定说明和状态文字。launcher 的固定中文文案全部从该子集绘制，因此 launcher 不再挂载 SD，也不再调用 `ink_fonts_load()`。

动态文件名不进入 flash 子集。photo 文件名继续使用 SD 上的 24px cpfont，并按旧固件的 `scale_divisor=2` 绘制；计数和辅助文字继续使用 16px footer 字体。

### 2. Launcher UI

保留旧版列表几何：列表 `x=24`、`y=50`、宽 `432`，卡片高 `70`，间距 `6`，图标 `x=40`，文字 `x=88`。页面标题使用 24px 固化字形；卡片标题采用旧版约 12px 的有效字号；说明文字使用 16px 字形。

按用户当前明确要求，选中标记采用 `12x4` 横线，而不是旧源码的 `4x46` 竖条。横线位于图标左侧，中心与 70px 卡片中心重合。选择变化时局刷区域只覆盖旧横线和新横线，并保留累计局刷策略。

### 3. Reader 启动

XTC/XTCH 书页是预渲染 framebuffer，正常打开书页不需要 cpfont。reader 启动时不再加载 24px 菜单字体和 18px 阅读字体，完成 SD 挂载、书籍扫描、首面加载后直接全刷。

无书、目录缺失、SD 错误和格式错误状态页使用固化 UI 字形或小号 ASCII fallback，避免为了错误页阻塞正常启动。状态标题和正文按旧版列表视觉比例缩小。

`ink_fonts` 在尝试候选路径前先用 `stat()` 检查普通文件。不存在的候选只记 DEBUG，不打印 WARN；存在但格式错误或读取失败的字体仍打印 WARN。最终所有候选均失败时保留一次明确的 role 级 WARN。

### 4. Photo 首屏与字体加载

photo 启动顺序为：初始化按键与 EPD、分配双灰阶 plane、挂载 SD、扫描并排序图片目录、寻找第一张可解码图片。候选解码失败时记录路径和原因并继续下一项；第一张成功解码的图片成为当前项。

第一张图片解码完成且图片文件关闭后，先创建一个一次性字体加载任务，再立即执行整屏双 plane 灰阶全刷。该任务只加载 24px menu cpfont 和 16px footer cpfont，设置完成状态后自行退出。任务不持有 EPD，不协调其他资源，也不跨 app 常驻。字体读取由此与 EPD 灰阶刷新等待并行。

字体状态使用一个简单的 FreeRTOS 同步原语发布，列表绘制只能在状态为 READY 后读取字体对象。字体尚未完成时，Back 始终有效；Confirm 显示固化的“正在加载”状态；Left/Right 暂不发起新的 SD 图片读取。任何路径都不使用未完成的字体对象，也不输出乱码。

### 5. Photo 交互与错误处理

photo 默认进入图片预览，而不是文件名列表：

- Left/Right：寻找对应方向的下一张可解码图片并整屏灰阶全刷，自动跳过损坏项。
- Confirm：字体 READY 后切换到当前图片所在位置的文件名列表。
- Back：短按立即切换到 launcher。

文件名列表中：

- Left/Right：移动选择，使用局刷；成功局刷累计 50 次后执行一次全刷并清零计数。
- Confirm：解码选中图片，成功后切回预览并整屏灰阶全刷；失败时继续寻找同方向下一张可解码图片。
- Back：短按立即切换到 launcher。

目录为空时首屏显示“未找到图片”。目录非空但所有候选均无法解码时显示“未找到可显示图片”。两种状态下 Back 均可返回 launcher。

### 6. 启动计时日志

三个 app 使用一致的阶段日志记录从 `APP_START` 起的累计耗时。至少记录：

- launcher：硬件初始化、首帧绘制、全刷完成。
- reader：SD 挂载、书籍扫描、首面解码、全刷完成。
- photo：SD 挂载、目录扫描、首张有效图片解码、字体任务开始/完成、灰阶全刷完成。

保留现有 `APP_START` 和 `BOOT_SWITCH` 日志。阶段日志用于区分 boot、SD、解码、字体和 EPD 刷新，不改变功能控制流。

## 测试与验收

### 主机测试

- launcher 选中横线位于图标左侧，尺寸为 `12x4`，并在 70px 卡片内垂直居中。
- launcher 选择局刷 region 同时覆盖旧选择和新选择的横线。
- photo 首图选择能跳过连续损坏项并返回第一张有效项。
- photo 空目录与全部损坏目录返回不同的状态结果。
- photo 默认 view 为 PREVIEW；Confirm 切 LIST；Back 的 boot switch 策略不变。
- 字体候选路径不存在时不调用 cpfont loader；存在路径仍进入加载流程。
- 24px 动态列表标题使用 divisor 2，header 保持 divisor 1，footer 保持 divisor 1。

### 构建与实机验证

使用 `C:\esp\v5.5.4\esp-idf` 和 `IDF_TOOLS_PATH=C:\Espressif` 分别构建 launcher、reader、photo。构建通过后烧录 COM9，按以下流程验证：

1. launcher 启动不挂载 SD、不打印字体错误，中文 UI 正常。
2. launcher 进入 reader，记录从 BOOT_SWITCH 到书页全刷完成的耗时。
3. reader 短按 Back 返回 launcher，不出现字体 WARN。
4. launcher 进入 photo，首屏直接显示第一张有效图片。
5. 在 photo 图片灰阶刷新期间确认字体任务并行完成；Confirm 进入文件名列表后中文名称正常且字号与旧版一致。
6. photo 预览和列表中短按 Back 均返回 launcher。
7. 串口日志不得出现 panic、assert failed、Guru Meditation、`sdmmc_read_sectors: not enough mem`、`panel not ready after sw reset`、`epd busy_wait aborted`、WiFi、voice note、I2S、ASR 或 USB MSC 初始化日志。

## 风险控制

- 字体任务只能在图片文件关闭后启动，避免两个任务同时操作同一 `FILE` 对象。
- 列表渲染必须等待字体 READY，避免读取正在初始化的 cpfont 结构。
- 字体任务创建失败时先完成图片全刷，再同步加载字体；不得因为同步 fallback 推迟首张图片出现，也不得退化为乱码列表。
- 图片跳过逻辑设置最多检查 catalog count 次，防止全部损坏时死循环。
- 固化字形仅覆盖固定 UI 文案；新增固定中文文案时必须同步更新字形生成清单和测试。
