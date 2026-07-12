# 多固件工作区架构

## 目标平台

本工作区面向 ESP32-S3，使用 ESP-IDF 5.5.4、16MB Flash 和 8MB Octal PSRAM。三个固件分别构建、分别烧录，每次只运行一个固件：

- `apps/launcher`：本地固件选择器，占用 factory 分区。
- `apps/reader`：XTC/XTCH 阅读器，占用 OTA slot 0。
- `apps/photo`：BMP 相册，占用 OTA slot 1。

每个子项目通过 `EXTRA_COMPONENT_DIRS` 引用工作区根目录的 `components`，不复制共享驱动或领域逻辑。各固件拥有独立的 `sdkconfig` 和构建目录，但使用相同的默认硬件配置与分区表。

## 统一配置

三个 `sdkconfig.defaults` 固定以下配置：

- target 为 `esp32s3`；
- Flash 容量为 16MB；
- 启用 Octal PSRAM，以 80MHz 运行并接入 heap allocator；
- 自定义分区表为 `../../partitions/partitions_16mb.csv`。

ESP-IDF 会在启动时自动探测 PSRAM 容量；8MB 是目标板硬件容量，不存在单独的 Kconfig 容量选项。

## 分区布局

| Label | 类型 | Offset | Size | 用途 |
| --- | --- | ---: | ---: | --- |
| `nvs` | data/nvs | `0x9000` | `0x6000` | 非易失配置 |
| `otadata` | data/ota | `0xF000` | `0x2000` | OTA 启动选择 |
| `phy_init` | data/phy | `0x11000` | `0x1000` | PHY 初始化数据 |
| `launcher` | app/factory | `0x20000` | `0x100000` | 启动器固件 |
| `reader` | app/ota_0 | `0x120000` | `0x400000` | 阅读器固件 |
| `photo` | app/ota_1 | `0x520000` | `0x400000` | 相册固件 |
| `data` | data/fat | `0x920000` | `0x6E0000` | 预留数据区 |

所有 app 分区按 `0x10000` 对齐。最后一个分区结束于 `0x1000000`，不超过 16MB Flash 边界。

## 构建边界

`apps/*/main/app_main.c` 将在后续任务中逐个实现。本阶段的 `main/CMakeLists.txt` 已声明该源文件，因此在入口文件创建之前不要求三个子项目构建成功。后续组件统一放在根目录 `components` 下，由三个固件按需链接。
