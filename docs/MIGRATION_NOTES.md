# 迁移记录

## Task 1：工作区骨架与统一配置

已创建 launcher、reader、photo 三个独立 ESP-IDF 子项目。三个项目共享根目录 `components`，使用 ESP32-S3、16MB Flash、8MB Octal PSRAM 的统一默认配置，并引用同一张 16MB 自定义分区表。

### 分区验证

验证环境：

- `IDF_PATH=C:\esp\v5.5.4\esp-idf`
- `IDF_TOOLS_PATH=C:\Espressif`
- Python：`C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe`

执行命令：

```powershell
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
$env:IDF_TOOLS_PATH='C:\Espressif'
& 'C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe' `
  "$env:IDF_PATH\components\partition_table\gen_esp32part.py" `
  --flash-size 16MB `
  partitions\partitions_16mb.csv `
  (Join-Path $env:TEMP 'ink-reader-v2-partitions.bin')
```

结果：命令退出码为 0，输出 3072 字节的二进制分区表；app 分区均按 `0x10000` 对齐，最大分区末端为 `0x1000000`，等于 16MB Flash 上限且未越界。将二进制表反解析后得到 `launcher=factory@0x20000/1M`、`reader=ota_0@0x120000/4M`、`photo=ota_1@0x520000/4M` 和 `data=fat@0x920000/7040K`。

### 当前限制

三个 `app_main.c` 尚未创建，本任务不要求应用构建成功。硬件初始化、EPD 显示、按键、SD 和启动分区切换均留待后续任务实现与上板验证。
