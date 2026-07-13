# 局部刷新与相册列表设计

## 目标

在不引入旧 runtime、display mailbox、aggressive interrupt 或资源协调器的前提下，完成三项交互调整：所有业务 app 短按 Back 返回 launcher；launcher 使用旧版左侧横线表示选中并进行区域局刷；photo 增加旧版样式的相册列表，列表区域局刷、照片预览四灰阶全刷。

## 交互行为

### launcher

- 首次进入 launcher 时绘制完整页面并执行一次全刷。
- Reader 和 Photo 均保持白底黑字，不使用整块黑底反白。
- 选中项使用旧版样式：在文字左侧绘制一条黑色横线。
- Left/Right 改变选中项，Confirm 启动对应 app。
- 选择变化时只更新旧横线与新横线覆盖的联合区域。

### reader

- Back 的按下边沿立即触发 `ink_boot_switch_to_launcher()`。
- 不再等待 1200ms 长按。
- 阅读、翻页和显示刷新策略保持现状。

### photo

- 启动后默认进入相册列表。
- 相册列表的页面样式、卡片尺寸、行高、间距、标题和选中样式以旧项目 `ink_photo_album_app` 与对应 renderer 为参考，只迁移纯布局参数和绘制规则。
- Left/Right 在相册条目间移动，Confirm 打开当前照片。
- 照片预览使用四灰阶全刷。
- 照片预览中 Confirm 返回相册列表。
- Back 在相册列表和照片预览中都立即返回 launcher。
- 空目录明确显示 `NO PHOTOS FOUND`，SD 或图片错误继续使用现有状态页。

## 刷新策略

### 区域局刷

`ink_hw` 新增同步区域局刷 API。实现只包含 GDEY0426T82 必需的窗口换算、RAM 写入、局刷波形和有超时的同步 BUSY 等待，不迁移旧驱动的 cancel callback、interrupt、phase、aborted error、mailbox 或 runtime 依赖。

launcher 每次选择变化只局刷两条选择横线的联合区域。相册列表每次选择变化只局刷旧选中行和新选中行的联合区域。

### 残影控制

- 相册列表首次进入时执行一次全刷。
- 每次成功的列表区域局刷将计数加一。
- 累计 50 次成功局刷后，下一次列表更新改用全刷，并将计数清零。
- 重新启动 photo app 时计数从零开始。
- launcher 的变化区域很小，不设置定期全刷；每次重新进入 launcher 已有一次完整全刷。

### 降级

区域局刷失败时打印包含区域和错误码的日志，并立即尝试全刷。全刷成功后清零对应局刷计数；全刷失败时保留错误日志并继续响应 Back，避免用户被困在业务 app。

## 组件边界

- `ink_hw`：提供全刷、四灰阶全刷和同步区域局刷，不感知 launcher 或相册状态。
- `ink_epd_ui`：绘制 launcher 横线选择样式和相册列表页面，不操作硬件。
- `ink_photo_core`：继续负责 BMP 目录、排序和解析，不负责页面状态。
- `apps/launcher/main/app_main.c`：保存 launcher 选中项并决定刷新区域。
- `apps/photo/main/app_main.c`：保存列表/预览模式、当前索引和局刷计数。
- `apps/reader/main/app_main.c`：只修改 Back 边沿处理。

不增加 app manager、插件接口、后台任务或跨 app 常驻状态。

## 验证

### 构建与静态检查

- 三个独立 ESP-IDF 项目均通过 `tools/build_all.ps1`。
- 排除项扫描继续确认没有 WiFi、voice note、ASR、I2S、USB MSC、旧 runtime、display mailbox 或资源协调器。
- UI 纯函数测试覆盖 launcher 横线位置、相册列表布局和刷新区域计算。
- 输入测试覆盖 Back 按下边沿，不再依赖 held time。
- 刷新策略测试覆盖第 1 至 49 次局刷、第 50 次累计以及下一次全刷后计数归零。

### COM9 实机验收

- launcher 首次全刷后，Left/Right 仅刷新左侧横线区域，文字不反白。
- reader 短按 Back 返回 launcher。
- photo 启动进入旧版样式相册列表，Left/Right 使用区域局刷。
- Confirm 打开照片并执行四灰阶全刷；再次 Confirm 返回列表。
- photo 任一视图短按 Back 返回 launcher。
- 连续导航超过 50 次，确认自动全刷一次后继续局刷。
- 日志不得出现 panic、assert failed、Guru Meditation、SD 内存不足、EPD reset/busy abort、WiFi、语音、I2S 或 USB MSC 相关内容。
