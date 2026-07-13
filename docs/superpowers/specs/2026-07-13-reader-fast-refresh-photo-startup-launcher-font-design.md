# Reader 快刷、相册启动与启动器字号设计

## 背景

COM9 日志显示，Photo 从 `APP_START` 到目录完成约 1.81 秒，首图解码约
2.34 秒，灰阶全刷完成约 6.60 秒。目录加载期间会逐个打开 BMP 并读取
header/palette，而首图显示阶段又会再次读取和解码同一文件，因此存在重复
SD I/O。

Reader 首屏和普通翻页当前都调用 `ink_hw_full_refresh()`。该路径执行面板
复位并以 `0x22/0xF7` 触发全刷，是翻页先反白再显示下一页的直接原因。
旧固件普通翻页则比较前后 framebuffer，计算最小变化区域并以
`0x22/0xFF` 执行区域快刷。

Launcher 的卡片标题使用 12px 固化短语，说明文字使用 16px，造成应用名
比说明文字更小。用户确认将应用名调整为 18px，并保持 flash 固化文字方案。

## 目标

1. Reader 普通翻页不再触发全刷反白，恢复旧版差异区域快刷行为。
2. 每第 50 次成功翻页执行一次维护性全刷，限制长期残影。
3. Photo 不在目录扫描阶段逐张预读 BMP，缩短进入首图前的等待。
4. 首张 BMP 解码失败时继续尝试下一张，最多扫描一圈。
5. Launcher 的“书库”和“相册”标题使用 18px 固化字形，并在 70px 卡片内
   保持清晰的标题/说明层级。

## Reader 刷新设计

Reader 继续使用当前独立 app 和 `ink_hw` 驱动，不迁移旧 runtime、display
mailbox 或刷新策略框架。

首次显示书页或状态页仍调用 `ink_hw_full_refresh()`，以建立明确的 panel
shadow。普通 Left/Right 翻页按以下流程执行：

1. 在 PSRAM 中保留一份当前屏幕对应的 480x800 1bpp framebuffer。
2. 将目标页事务式加载到当前 framebuffer。
3. 比较前后帧，计算包含所有变化像素的最小矩形。
4. 有变化时调用 `ink_hw_partial_refresh_area()`；驱动负责字节边界对齐、写入
   当前/上一 plane，并以 `0xFF` 激活快刷。
5. 前 49 次快刷成功后提交当前页和屏幕副本，并增加成功翻页计数。
6. 第 50 次翻页使用维护性全刷，成功后清零计数。
7. 刷新失败时恢复原 framebuffer 和原页码，避免模型与屏幕不同步。

日志区分 `PAGE_REFRESH mode=partial`、`PAGE_REFRESH mode=cleanup_full` 和
`FIRST_REFRESH mode=full`，便于 COM9 验证。

## Photo 启动设计

`ink_photo_catalog_load()` 只负责：

- 打开 `/sdcard/photos`；
- 收集非隐藏的 `.bmp` 文件名和路径；
- 按现有不区分大小写规则排序；
- 限制条目数量和路径长度。

目录阶段不再调用 `probe_bmp_file()`。实际可显示性由现有
`ink_photo_catalog_find_decodable()` 和 `ink_photo_decode_bmp()` 判定。首项失败
就继续下一项，最多检查目录中的每个候选一次。全部失败时显示
“未找到可显示图片”。列表可以显示扩展名合法但内容损坏的 BMP；用户确认进入
该项时仍按同一规则跳到下一张可显示图片。

字体任务继续与首图灰阶全刷并行，不新增第二个目录后台任务，不引入 SD 访问
协调器。

## Launcher 字号设计

生成器增加 18px 的“书库”和“相册”固化短语，替换当前 12px 资产。页面标题
保持 24px，卡片说明保持 16px，卡片几何保持 `432x70`、间距 `6`、图标和
选中横线位置不变。

卡片内标题与说明重新设置 y 坐标，使两行内容作为一个文字组在 70px 高度内
视觉居中，且不与底部分隔线重叠。Launcher 仍不挂载 SD、不加载 cpfont。

## 错误处理

- Reader 无法分配上一帧缓冲时显示明确错误并保留 Back 返回 Launcher。
- Reader 快刷失败时回滚页面状态，不自动用全刷掩盖失败。
- Photo 目录打开失败仍显示目录错误；空目录显示“未找到图片”；全部候选损坏
  显示“未找到可显示图片”。
- 所有 app 的 Back 短按切换行为保持不变。

## 验证

自动化验证包括：

- Reader 首刷仍为 full，普通翻页只走 partial，第 50 次走 cleanup full。
- 差异区域覆盖相同帧、单像素、多区域、边界和无变化情况。
- Photo catalog 不再调用 BMP probe，仍保留排序、空目录和候选上限行为。
- 固化资产包含 18px“书库/相册”，生成结果与仓库文件一致。
- Launcher 卡片自检验证 18px 标题实际落点和文字组边界。

构建并烧录三张镜像后，在 COM9 验证：Photo 目录阶段耗时下降；Reader 普通
翻页日志只出现 partial 且屏幕不反白；Launcher 应用名大小与卡片层级符合确认
结果；串口不出现 panic、assert、EPD timeout 或禁止模块日志。
