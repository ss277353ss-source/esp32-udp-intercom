# ESP32 UDP 语音对讲机

基于 ESP32 的按键对讲（Push-to-Talk）设备：按住按键说话，松开按键收听。音频以 16 kHz / 16 bit PCM 打包成 UDP 数据报，在同一 WiFi 内广播，或经由公网 UDP 中继服务器跨网段传输。板载 SSD1306 OLED 显示网络状态与工作状态。

## 功能特性

- 单按键半双工对讲，25 ms 软件消抖
- INMP441 I2S 数字麦克风录音，MAX98357A I2S 功放放音
- 发送端带噪声门限、增益与峰值检测，静音时不上行
- 两种组网方式：局域网 UDP 广播 / 公网 UDP 中继
- WiFi 断线自动重连，OLED 实时显示 IP 与状态
- 中继服务端为单文件 Node.js 程序，无第三方依赖

## 硬件清单

| 模块 | 型号 | 说明 |
| --- | --- | --- |
| 主控 | ESP32 DevKit (esp32dev) | 4 MB Flash |
| 麦克风 | INMP441 | I2S 输出，L/R 接 GND 选择左声道 |
| 功放 | MAX98357A | I2S 输入，接 8 Ω 喇叭 |
| 显示屏 | SSD1306 128x64 | I2C，地址 0x3C |
| 按键 | 轻触开关 | 一端接 GPIO18，一端接 GND |

### 引脚连接

| 信号 | ESP32 引脚 | 备注 |
| --- | --- | --- |
| OLED SDA | GPIO21 | I2C |
| OLED SCL | GPIO22 | I2C |
| PTT 按键 | GPIO18 | `INPUT_PULLUP`，低电平有效 |
| INMP441 BCLK | GPIO26 | 与功放共用 |
| INMP441 WS | GPIO25 | 与功放共用 |
| INMP441 SD | GPIO33 | 麦克风数据输入 |
| MAX98357A BCLK | GPIO26 | 与麦克风共用 |
| MAX98357A WS | GPIO25 | 与麦克风共用 |
| MAX98357A DIN | GPIO32 | 功放数据输出 |

> 麦克风与功放共用 BCLK/WS，固件在同一时刻只安装 I2S RX 或 TX 其中一个驱动（`I2S_NUM_0` 交替使用），因此可以共用时钟线。若录音期间喇叭有明显杂音，可在 DIN 上加下拉电阻。

## 目录结构

```
├── platformio.ini          # PlatformIO 工程配置
├── src/
│   ├── main.cpp            # 状态机与主循环
│   ├── config.h            # 引脚、音频参数、网络模式
│   ├── secrets.example.h   # 私有配置模板（可提交）
│   ├── audio.cpp/.h        # I2S 录音与放音
│   ├── network.cpp/.h      # WiFi、UDP 收发、中继
│   ├── oled.cpp/.h         # OLED 界面
│   └── button.cpp/.h       # 按键封装
├── server/
│   └── relay-server.js     # 公网 UDP 中继服务
└── README.md
```

> 实验报告生成脚本（`*.py`、`generate-*.js`）与 docx 产物仅保留在本地，已通过 `.gitignore` 排除，不属于本仓库内容。

## 快速开始

1. 安装 [PlatformIO](https://platformio.org/)，用 VS Code 或命令行打开本工程。
2. 复制私有配置模板并填写自己的参数：

   ```bash
   cp src/secrets.example.h src/secrets.h
   ```

3. 编辑 `src/secrets.h`：

   ```c
   #define WIFI_SSID "你的WiFi名称"
   #define WIFI_PASSWORD "你的WiFi密码"
   #define RELAY_HOST "你的中继服务器地址"
   #define RELAY_PORT 4210
   ```

4. 在 `src/config.h` 中选择组网模式（默认中继模式）：

   ```c
   #define NETWORK_MODE NETWORK_MODE_RELAY   // 或 NETWORK_MODE_LAN
   ```

5. 编译并烧录，串口波特率 115200：

   ```bash
   pio run -t upload
   pio device monitor
   ```

> `src/secrets.h` 已被 `.gitignore` 排除，不会进入版本库；仓库中只保留占位符版本的 `secrets.example.h`。未创建 `secrets.h` 时工程仍可编译，会回退到示例配置并给出编译警告。

## 网络模式

| 模式 | 说明 | 适用场景 |
| --- | --- | --- |
| `NETWORK_MODE_LAN` | UDP 广播到 `255.255.255.255:AUDIO_UDP_PORT` | 所有设备在同一局域网 |
| `NETWORK_MODE_RELAY` | UDP 单播到 `RELAY_HOST:RELAY_PORT`，由服务器转发 | 设备位于不同网络 / 跨 NAT |

中继模式下设备每秒发送一次空包作为保活，服务端 30 秒未收到数据即判定离线。

### 部署中继服务器

```bash
node server/relay-server.js          # 默认监听 UDP 4210
PORT=5000 node server/relay-server.js # 自定义端口
```

需要在云服务器安全组 / 防火墙放行对应的 **UDP** 端口。服务端会把收到的音频转发给除发送者外的所有在线客户端，因此请勿在公网上暴露无鉴权的实例。

## UDP 数据包格式

头部 14 字节，小端序，`__attribute__((packed))`：

| 字段 | 长度 | 说明 |
| --- | --- | --- |
| `magic` | 4 B | 固定 `0x31544B45`（ASCII `EKT1`） |
| `deviceId` | 4 B | 由 MAC 地址后 4 字节生成，用于过滤自己的回声 |
| `sequence` | 4 B | 递增序号（当前仅发送端使用） |
| `sampleCount` | 2 B | 后续 PCM 采样点数，0 表示保活包 |
| `payload` | N × 2 B | int16 小端 PCM 采样 |

## 工作状态机

- `READY`：待机，显示 IP 与 Waiting，收到音频包后切到 `RECEIVING`
- `TALKING`：按住按键，I2S 以 RX 模式录音并上行；松开回到 `READY`
- `RECEIVING`：I2S 以 TX 模式放音；超过 300 ms 没有新包则回到 `READY`

## 音频参数

在 `src/config.h` 中调整：

| 宏 | 默认值 | 说明 |
| --- | --- | --- |
| `SAMPLE_RATE` | 16000 | 采样率 |
| `AUDIO_BLOCK_SIZE` | 512 | 每包采样点数（约 32 ms） |
| `STREAM_GAIN` | 2 | 发送前增益 |
| `AUDIO_NOISE_GATE` | 260 | 噪声门限，低于该值置零 |
| `VOICE_SEND_THRESHOLD` | 650 | 峰值低于该值不发包 |
| `SPEAKER_VOLUME_PERCENT` | 20 | 播放音量百分比 |
| `MIC_USE_RIGHT_CHANNEL` | 0 | 麦克风无输出时改为 1 |

## 常见问题

- **OLED 一直显示 Offline**：检查 `src/secrets.h` 中的 WiFi 名称与密码，串口会打印连接过程与失败原因。
- **有网但两台设备互相听不到**：中继模式需确认服务器在线、UDP 端口已放行；同局域网建议改用 `NETWORK_MODE_LAN`。
- **喇叭无声**：确认 MAX98357A 供电与 SD_MODE 接法，并检查 `SPEAKER_VOLUME_PERCENT`。
- **录音全是静音**：确认 INMP441 的 L/R 已接地，必要时把 `MIC_USE_RIGHT_CHANNEL` 改为 1。
- **播放断续 / 有咔哒声**：发送端静音门槛会让接收端频繁重建 I2S 驱动，可适当调低 `VOICE_SEND_THRESHOLD` 或延长接收端的 300 ms 超时。
- **OLED 不亮**：确认 I2C 地址是否为 0x3C，部分模块为 0x3D，可修改 `OLED_ADDR`。
