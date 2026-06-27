# ink-reader 固件模块接线

本文档按当前仓库代码整理固件内各模块的接线关系，分为两类：

- 主固件当前启用的接线
- 项目内已有、但当前主固件未启用的测试/探针接线

这样可以避免把实验代码里的外设定义误认为主固件正式接线。

## 1. 主固件当前使用的接线

| 模块 | 信号 | GPIO | 备注 |
| --- | --- | --- | --- |
| 电子纸 EPD | MOSI | GPIO4 | SPI 数据 |
| 电子纸 EPD | SCLK | GPIO5 | SPI 时钟 |
| 电子纸 EPD | CS | GPIO6 | 片选 |
| 电子纸 EPD | DC | GPIO7 | 数据/命令选择 |
| 电子纸 EPD | RST | GPIO15 | 复位 |
| 电子纸 EPD | BUSY | GPIO16 | 忙信号输入 |
| TF / SD 卡 | CLK | GPIO40 | SDMMC 4-bit |
| TF / SD 卡 | CMD | GPIO39 | SDMMC 4-bit |
| TF / SD 卡 | D0 | GPIO41 | SDMMC 4-bit |
| TF / SD 卡 | D1 | GPIO42 | SDMMC 4-bit |
| TF / SD 卡 | D2 | GPIO48 | SDMMC 4-bit |
| TF / SD 卡 | D3 | GPIO38 | SDMMC 4-bit |
| 按键 | Back | GPIO9 | 低电平有效 |
| 按键 | Confirm | GPIO10 | 低电平有效 |
| 按键 | Right | GPIO11 | 低电平有效 |
| 按键 | Left | GPIO12 | 低电平有效 |
| 按键 | Power | GPIO46 | 低电平有效 |
| 串口控制台 | TX | GPIO43 | UART0, 115200 |
| 串口控制台 | RX | GPIO44 | UART0, 115200 |

## 2. 主固件当前启用模块说明

### 2.1 电子纸显示

- 屏幕驱动当前使用 SPI2_HOST
- 屏幕型号为 `GDEY0426T82`
- SPI 时钟配置为 `20 MHz`

### 2.2 TF / SD 卡

- 当前通过 `SDMMC_HOST_DEFAULT()` 挂载
- 使用 4-bit SDMMC 总线
- 已占用 `GPIO38 / 39 / 40 / 41 / 42 / 48`

### 2.3 按键输入

- 当前固件定义了 5 个按键：`Back / Confirm / Left / Right / Power`
- 所有按键均为低电平有效

### 2.4 WiFi

- WiFi 使用 ESP32-S3 片上无线模块
- 当前代码中没有单独的外部 GPIO 接线定义

### 2.5 NVS / PSRAM / Flash

- 这些模块属于芯片或板级资源
- 当前业务代码中没有额外的“模块接线 GPIO”需要单独列出

## 3. 项目内测试或验证过的接线

以下接线来自仓库中的测试工程或硬件探针工程，说明这些外设在项目里出现过，但它们不是当前主固件正式启用的接线说明。

### 3.1 MPU6050 / GY6500 倾角传感器

| 来源 | 信号 | GPIO | 备注 |
| --- | --- | --- | --- |
| `tests/tilt_grid` | SCL | GPIO1 | `I2C_NUM_0`, `100000 Hz` |
| `tests/tilt_grid` | SDA | GPIO2 | `I2C_NUM_0`, `100000 Hz` |
| `tests/hw_probe` | SCL | GPIO1 | `I2C_NUM_0`, `400000 Hz` |
| `tests/hw_probe` | SDA | GPIO2 | `I2C_NUM_0`, `400000 Hz` |

说明：

- `tests/tilt_grid` 和 `tests/hw_probe` 都把 MPU I2C 接在 `GPIO1/2`
- 两者差异主要在 I2C 速率配置不同

### 3.2 麦克风 PDM

| 来源 | 信号 | GPIO | 备注 |
| --- | --- | --- | --- |
| `tests/hw_probe` | PDM CLK | GPIO17 | 麦克风时钟 |
| `tests/hw_probe` | PDM DATA | GPIO18 | 麦克风数据 |

### 3.3 扬声器 I2S

| 来源 | 信号 | GPIO | 备注 |
| --- | --- | --- | --- |
| `tests/hw_probe` | DOUT | GPIO8 | I2S 数据输出 |
| `tests/hw_probe` | BCLK | GPIO14 | I2S 位时钟 |
| `tests/hw_probe` | LRCLK | GPIO13 | I2S 左右声道时钟 |

## 4. 接线占用速查

### 4.1 当前主固件已占用 GPIO

`GPIO4`
`GPIO5`
`GPIO6`
`GPIO7`
`GPIO9`
`GPIO10`
`GPIO11`
`GPIO12`
`GPIO15`
`GPIO16`
`GPIO38`
`GPIO39`
`GPIO40`
`GPIO41`
`GPIO42`
`GPIO43`
`GPIO44`
`GPIO46`
`GPIO48`

### 4.2 项目测试代码中额外出现过的 GPIO

`GPIO1`
`GPIO2`
`GPIO8`
`GPIO13`
`GPIO14`
`GPIO17`
`GPIO18`

## 5. 代码来源

本文件内容整理自以下代码与日志：

- `main/app_main.c`
- `main/ink_app_boot.c`
- `components/ink_hw/ink_button_input.c`
- `tests/tilt_grid/main/app_main.c`
- `tests/hw_probe/main/app_main.c`
- `sdkconfig`
- `tmp/launcher_stage1_serial_capture.txt`

其中串口控制台引脚由两部分共同确认：

- `sdkconfig` 指定控制台为 `UART0`、波特率 `115200`
- 启动日志显示 `GPIO 44 and 43 are used as console UART I/O pins`
