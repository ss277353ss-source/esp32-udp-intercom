# ESP32 UDP 语音对讲机

一个基于 ESP32 的按键对讲（Push-to-Talk，PTT）项目：**按住按键说话，松开按键收听**。音频以 16 kHz / 16 bit PCM 采集，打包成 UDP 数据报，在同一 WiFi 内广播，或经公网 UDP 中继服务器跨网络传输。板载 0.96 寸 OLED 显示网络状态、IP 地址和当前工作状态。

本文档面向第一次接触 ESP32 的读者，按照「硬件接线 → 安装软件 → 填写 WiFi → 编译烧录 → 对讲测试」的顺序，一步步照做即可完成。

---

## 目录

1. [功能特性](#1-功能特性)
2. [硬件准备与接线](#2-硬件准备与接线)
3. [软件环境安装](#3-软件环境安装)
4. [修改 WiFi 名称和密码（必做）](#4-修改-wifi-名称和密码必做)
5. [编译与烧录](#5-编译与烧录)
6. [使用方法](#6-使用方法)
7. [两种组网方式](#7-两种组网方式)
8. [工作原理](#8-工作原理)
9. [参数调节](#9-参数调节)
10. [常见问题](#10-常见问题)
11. [目录结构](#11-目录结构)
12. [许可证](#12-许可证)

---

## 1. 功能特性

- 单按键半双工对讲，25 ms 软件消抖，按下即说、松开即听
- INMP441 数字麦克风（I2S）录音，MAX98357A 功放（I2S）放音
- 发送端带噪声门限、增益和峰值检测，检测到静音时不上传，节省带宽
- 两种组网方式：局域网 UDP 广播 / 公网 UDP 中继，后者可跨路由器、跨网络对讲
- WiFi 断线自动重连，OLED 实时显示 IP 和状态
- 中继服务端是单文件 Node.js 程序，只用内置 `dgram` 模块，无需安装任何 npm 包

需要 **两台设备**（每台一套硬件）才能完成对讲测试。

---

## 2. 硬件准备与接线

### 2.1 物料清单

| 名称 | 型号 / 规格 | 数量（每台设备） | 说明 |
| --- | --- | --- | --- |
| 主控板 | ESP32 DevKit（`esp32dev`，4 MB Flash） | 1 | 常见的 30 针或 38 针开发板均可 |
| 数字麦克风 | INMP441 | 1 | I2S 输出，3.3 V 供电 |
| 音频功放 | MAX98357A | 1 | I2S 输入，自带解码 |
| 显示屏 | SSD1306 128×64，I2C 接口 | 1 | 地址通常为 `0x3C` |
| 喇叭 | 8 Ω 2~3 W | 1 | 接功放输出 |
| 按键 | 6×6 轻触开关 | 1 | 一端接 GPIO18，一端接 GND |
| 连接件 | 面包板 + 杜邦线 | 若干 | 也可直接焊接 |

### 2.2 接线总览

```
                        ┌────────────────────┐
      INMP441           │       ESP32        │          MAX98357A
    ┌──────────┐        │                    │         ┌───────────┐
    │ VDD  3V3 ├────────┤ 3V3            5V  ├─────────┤ VIN       │
    │ GND      ├────────┤ GND            GND ├─────────┤ GND       │
    │ L/R  GND │        │                    │         │           │
    │ SCK      ├────────┤ GPIO26 (BCLK)      ├─────────┤ BCLK      │
    │ WS       ├────────┤ GPIO25 (WS)        ├─────────┤ LRC       │
    │ SD       ├────────┤ GPIO33             │         │           │
    └──────────┘        │ GPIO32 ────────────┼─────────┤ DIN       │
                        │                    │         └─────┬─────┘
                        │ GPIO21 ── OLED SDA │               │
                        │ GPIO22 ── OLED SCL │            喇叭 8Ω
                        │ GPIO18 ── 按键 ── GND
                        └────────────────────┘
```

> 上图仅为连线关系示意，实际请以下面的分模块引脚表为准。两个模块的 BCLK 和 WS 接到 ESP32 的**同一个**引脚上。

### 2.3 INMP441 麦克风

| INMP441 引脚 | ESP32 引脚 | 说明 |
| --- | --- | --- |
| VDD | 3V3 | 只能接 3.3 V |
| GND | GND | 必须共地 |
| L/R | GND | 接地选择左声道（对应 `MIC_USE_RIGHT_CHANNEL 0`） |
| SCK | GPIO26 | 位时钟 BCLK |
| WS | GPIO25 | 声道选择 WS |
| SD | GPIO33 | 麦克风数据输入到 ESP32 |

### 2.4 MAX98357A 功放

| MAX98357A 引脚 | 连接到 | 说明 |
| --- | --- | --- |
| VIN | 5V（ESP32 的 5V 引脚） | 功放需要 5 V，不要接 3.3 V |
| GND | GND | 与 ESP32 共地 |
| LRC | GPIO25 | 与麦克风 WS 共用 |
| BCLK | GPIO26 | 与麦克风 SCK 共用 |
| DIN | GPIO32 | ESP32 音频数据输出 |
| SD_MODE | VIN（高电平） | 保持使能；部分模块已默认上拉，可悬空 |
| GAIN | 悬空 | 默认增益，接 GND 会更大声 |
| OUT+ / OUT- | 喇叭两端 | 8 Ω 喇叭，注意功率 |

### 2.5 OLED 显示屏

| OLED 引脚 | ESP32 引脚 | 说明 |
| --- | --- | --- |
| VCC | 3V3 | 3.3 V 供电 |
| GND | GND | 共地 |
| SDA | GPIO21 | I2C 数据 |
| SCL | GPIO22 | I2C 时钟 |

### 2.6 按键

按键一端接 **GPIO18**，另一端接 **GND**。固件使用内部上拉，按键按下时读到低电平，不需要外接电阻。

### 2.7 接线注意事项

1. **所有模块必须共地**，否则 I2S 和 I2C 都可能不稳定。
2. 麦克风和功放**共用 GPIO25 / GPIO26** 两根时钟线，这是设计如此：固件在同一时刻只启用录音或放音其中一路（`I2S_NUM_0` 交替使用）。
3. MAX98357A 尽量用 5 V 供电；如果用 USB 供电且喇叭较大声，可能出现重启，建议给功放单独供电（仍需共地）。
4. 喇叭接到 `OUT+` / `OUT-`，不要接到 GND。
5. 接线前先断电，确认无误后再上电，避免电源反接烧模块。

---

## 3. 软件环境安装

### 3.1 安装 VS Code

到 [code.visualstudio.com](https://code.visualstudio.com/) 下载并安装 Visual Studio Code（Windows / macOS / Linux 均可）。

### 3.2 安装 PlatformIO 插件

1. 打开 VS Code，点击左侧活动栏的「扩展」图标（四个方块）。
2. 搜索 `PlatformIO IDE`，选择官方插件，点击 **Install**。
3. 安装完成后，左侧会出现一个蚂蚁头形状的 PlatformIO 图标，底部状态栏会出现 PlatformIO 工具栏。

### 3.3 获取本项目

任选一种方式：

- 已经拿到文件夹：直接进入第 3.4 步。
- 从 GitHub 克隆（需要安装 Git）：

  ```bash
  git clone <本仓库地址>
  ```

- 或者下载仓库压缩包，解压到任意目录。

### 3.4 用 VS Code 打开项目

1. 菜单 **File → Open Folder…**，选择项目根目录（包含 `platformio.ini` 的那一层）。
2. 首次打开时 PlatformIO 会自动读取 `platformio.ini`，在右下角提示安装 ESP32 平台和工具链，**需要联网，可能要几分钟到十几分钟**，请耐心等待进度条结束。
3. 依赖库（Adafruit GFX、Adafruit SSD1306）已写在 `platformio.ini` 中，PlatformIO 会自动下载，无需手动安装。

### 3.5 安装 USB 串口驱动

如果电脑插上 ESP32 后在设备管理器里看不到串口：

- 板载 CP2102 芯片：安装 Silicon Labs CP210x 驱动。
- 板载 CH340 / CH341 芯片：安装 WCH CH340 驱动。

安装后在「设备管理器 → 端口（COM 和 LPT）」中应能看到类似 `Silicon Labs CP210x (COM3)` 的设备，记下 COM 号。

---

## 4. 修改 WiFi 名称和密码（必做）

项目不会把真实 WiFi 信息写进代码，而是放在一个**本地私有文件** `src/secrets.h` 中。该文件已被 `.gitignore` 排除，不会上传到 GitHub。仓库里只有模板文件 `src/secrets.example.h`。

### 4.1 复制模板文件

在项目根目录下执行（Windows PowerShell）：

```powershell
Copy-Item src\secrets.example.h src\secrets.h
```

也可以在文件资源管理器中进入 `src` 文件夹，复制 `secrets.example.h`，粘贴后重命名为 `secrets.h`。

### 4.2 填写 WiFi 信息

用 VS Code 打开 `src/secrets.h`，修改下面两行：

```c
#define WIFI_SSID "你的WiFi名称"
#define WIFI_PASSWORD "你的WiFi密码"
```

例如：

```c
#define WIFI_SSID "MyHomeWiFi"
#define WIFI_PASSWORD "12345678"
```

注意事项：

- 必须使用**英文双引号**，行尾不要多余的分号。
- ESP32 只支持 **2.4 GHz** WiFi，5 GHz 网络连不上。
- 名称和密码区分大小写，注意不要带多余空格。
- 可以使用手机热点，但需要把热点频段设置为 2.4 GHz。
- 该文件只保存在本地，修改后按 `Ctrl + S` 保存即可。

### 4.3 选择组网模式

打开 `src/config.h`，找到这一行：

```c
#define NETWORK_MODE NETWORK_MODE_RELAY
```

- 两台设备连**同一个路由器**：改成

  ```c
  #define NETWORK_MODE NETWORK_MODE_LAN
  ```

- 两台设备在**不同网络**（例如一台在家、一台在学校）：保持 `NETWORK_MODE_RELAY`，并继续第 4.4 步。

### 4.4 填写中继服务器地址（仅中继模式）

继续在 `src/secrets.h` 中填写你自己的服务器：

```c
#define RELAY_HOST "你的服务器公网IP"
#define RELAY_PORT 4210
```

如果没有服务器，就先用局域网模式（`NETWORK_MODE_LAN`），中继服务的部署方法见 [7.2 节](#72-公网中继模式)。

---

## 5. 编译与烧录

### 5.1 连接开发板

用**能传输数据**的 USB 线（不是只能充电的线）把 ESP32 接到电脑，确认设备管理器里出现了串口。

### 5.2 使用 VS Code 按钮

打开项目后，VS Code 底部状态栏有一排 PlatformIO 按钮：

| 按钮 | 作用 |
| --- | --- |
| ✓（Build） | 编译 |
| →（Upload） | 编译并烧录 |
| 插头图标（Monitor） | 打开串口监视器 |
| 垃圾桶（Clean） | 清理编译产物 |

推荐顺序：先点 ✓ 编译，确认没有红色报错；再点 → 烧录；最后点插头打开串口，波特率 `115200`。

### 5.3 使用命令行

在项目根目录执行：

```bash
pio run                 # 编译
pio run -t upload       # 编译并烧录
pio device monitor      # 串口监视器（Ctrl+C 退出）
```

### 5.4 烧录成功后的串口输出

正常启动时串口大致会打印：

```
ESP32 Intercom V2 UDP
Device ID: 1A2B3C4D
WiFi connecting....
WiFi connected, IP=192.168.1.50
UDP port=4210
Network mode: RELAY
Relay resolved: <你的服务器地址> -> <解析结果>
Init done
State: READY
```

看到 `Init done` 和 `State: READY` 就说明固件启动成功。若一直打印 `WiFi connecting....` 后出现 `WiFi connect failed`，请回到第 4 节检查 WiFi 名称和密码。

### 5.5 烧录失败怎么办

- **找不到端口**：换 USB 线、换 USB 口，或安装第 3.5 节的串口驱动。
- **Connecting........_____ 后超时**：按住开发板上的 `BOOT` 键，再点上传，看到 `Connecting` 后松开。
- **端口被占用**：关闭其他串口工具（串口助手、Arduino IDE 等）后再试。
- **一直复位重启**：多为供电不足，换 USB 口或使用带独立供电的 USB Hub。

---

## 6. 使用方法

### 6.1 上电

给两块 ESP32 分别上电。等待几秒，OLED 会显示：

```
==================
       ESP TALK
WiFi : Connected
IP   : 192.168.1.50
Status:
Waiting...
```

| OLED 显示 | 含义 |
| --- | --- |
| `WiFi : Connected` | 已连接 WiFi |
| `WiFi : Offline` | 未连接，正在自动重连 |
| `IP : x.x.x.x` | 当前局域网 IP |
| `Waiting...` | 待机，等待对方说话 |
| `Talking...` | 正在录音并发送 |
| `Receiving...` | 正在播放对方的声音 |

### 6.2 对讲操作

1. 确认两台设备都已显示 `WiFi : Connected`。
2. 在设备 A 上**按住**按键不放，对着麦克风正常说话，OLED 显示 `Talking...`。
3. 设备 B 会自动显示 `Receiving...` 并从喇叭放出声音。
4. 松开按键，设备 A 回到 `Waiting...`，此时可以听设备 B 说话。
5. 设备 B 需要说话时，同样按住它自己的按键。

这是**半双工**对讲：同一时刻只有一方能说。双方同时按住时，后按的一方会抢占，建议轮流说话。

### 6.3 首次联调建议

1. 先只给一块设备上电，打开串口确认 `WiFi connected`、IP 正常。
2. 中继模式下再确认打印了 `Relay resolved`，没有 `Relay DNS failed`。
3. 两块设备都确认无误后，再按住按键测试。
4. 如果对方听不到，先看发送端串口有没有 `UDP send failed`，再看接收端是否进入 `Receiving...`。

---

## 7. 两种组网方式

### 7.1 局域网模式

把 `src/config.h` 中的 `NETWORK_MODE` 改为 `NETWORK_MODE_LAN`。设备会把音频包广播到 `255.255.255.255:4210`，同一局域网内的其他设备都能收到。

适用条件：

- 两台设备连接同一个路由器 / 同一个手机热点；
- 路由器没有开启「AP 隔离 / 客户端隔离」，否则设备之间无法互相通信；
- 网络是 2.4 GHz。

优点是不需要服务器、延迟最低；缺点是无法跨网络使用。

### 7.2 公网中继模式

设备把音频发给你的公网服务器，服务器再转发给其他在线设备，因此可以跨路由器、跨城市使用。默认端口为 UDP `4210`。

#### 第一步：准备服务器

一台有公网 IP 的 Linux 云服务器（Ubuntu 20.04 及以上即可，配置最低 1 核 1 G）。在云厂商控制台的**安全组**中放行 **UDP 4210** 入方向；如果系统开启了 ufw，再执行：

```bash
sudo ufw allow 4210/udp
```

#### 第二步：安装 Node.js

```bash
sudo apt update
sudo apt install -y nodejs
node -v        # 能输出版本号即可，本程序只用到内置模块，Node 12 及以上均可
```

#### 第三步：上传中继程序

在**你自己的电脑**上，于项目根目录执行：

```bash
scp server/relay-server.js 用户名@服务器IP:~/relay-server.js
```

#### 第四步：前台试运行

登录服务器后执行：

```bash
node ~/relay-server.js
```

看到下面的日志说明启动成功：

```
ESP32 intercom UDP relay listening on 0.0.0.0:4210
```

保持运行，然后给 ESP32 上电。设备每秒会发一次保活包，服务器日志中应出现：

```
client online x.x.x.x:4210 device=1a2b3c4d
```

两台设备都上线后，按住任意一台的按键，另一台就能听到声音。

#### 第五步：后台常驻（可选）

按 `Ctrl+C` 停掉前台进程，创建 systemd 服务：

```bash
sudo tee /etc/systemd/system/esp32-relay.service > /dev/null <<'EOF'
[Unit]
Description=ESP32 UDP Relay
After=network.target

[Service]
ExecStart=/usr/bin/node /home/ubuntu/relay-server.js
Restart=always
User=ubuntu

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now esp32-relay
sudo systemctl status esp32-relay
```

注意把 `User` 和 `ExecStart` 中的路径换成你自己的用户名和文件位置。查看日志：

```bash
sudo journalctl -u esp32-relay -f
```

#### 安全提醒

中继程序**没有任何鉴权**，任何人只要知道 IP 和端口就能收到音频，也能往里注入音频。请只在可信环境使用，不要把端口暴露给不相关的人；如果长期公开使用，建议自行增加设备白名单或简单令牌校验。

---

## 8. 工作原理

### 8.1 状态机

- `READY`：待机。每秒刷新一次 OLED 的网络信息；收到音频包则进入 `RECEIVING`。
- `TALKING`：按键按住期间，I2S 以 RX 模式录音，做直流消除、噪声门限、增益和峰值检测，符合条件的音频打包上行。
- `RECEIVING`：I2S 以 TX 模式播放收到的音频；超过 300 ms 没有新包则回到 `READY`。

按键读取带 25 ms 消抖，因此轻触不会误触发。

### 8.2 UDP 数据包格式

头部固定 14 字节，小端序（结构体使用 `__attribute__((packed))`，与 Node.js 服务端逐字节对应）：

| 字段 | 长度 | 说明 |
| --- | --- | --- |
| `magic` | 4 B | 固定 `0x31544B45`，即 ASCII `EKT1` |
| `deviceId` | 4 B | 由 MAC 地址后 4 字节生成，用于过滤自己的回声 |
| `sequence` | 4 B | 递增序号，当前仅发送端使用 |
| `sampleCount` | 2 B | 后续 PCM 采样点数；为 0 表示保活包 |
| `payload` | N × 2 B | int16 小端 PCM 采样数据 |

每包默认 512 个采样点（约 32 ms），采样率 16 kHz，单声道。

### 8.3 中继转发规则

服务器按「IP:端口」维护在线客户端，30 秒未收到数据判定离线；收到音频后转发给除发送者以外的所有在线客户端，并按 `deviceId` 过滤，避免把自己的声音回传给自己。

---

## 9. 参数调节

全部参数都在 `src/config.h` 中：

| 宏 | 默认值 | 作用 |
| --- | --- | --- |
| `SAMPLE_RATE` | 16000 | 采样率，越高音质越好、带宽越大 |
| `AUDIO_BLOCK_SIZE` | 512 | 每包采样点数，约 32 ms |
| `STREAM_GAIN` | 2 | 发送前的数字增益 |
| `AUDIO_NOISE_GATE` | 260 | 噪声门限，低于该值直接置零 |
| `VOICE_SEND_THRESHOLD` | 650 | 峰值低于该值不发包，防止静音占用带宽 |
| `SPEAKER_VOLUME_PERCENT` | 20 | 播放音量百分比，觉得小声可调大 |
| `MIC_USE_RIGHT_CHANNEL` | 0 | 麦克风无输出时改为 1 试试 |
| `RELAY_KEEPALIVE_MS` | 1000 | 中继模式保活间隔 |

调节建议：

- 对方听不清、声音太小：先调大 `SPEAKER_VOLUME_PERCENT`，再考虑 `STREAM_GAIN`。
- 环境噪声大、经常自动发空包：调高 `AUDIO_NOISE_GATE` 和 `VOICE_SEND_THRESHOLD`。
- 说话开头被截断：适当降低 `VOICE_SEND_THRESHOLD`。
- 声音断续：可降低 `STREAM_GAIN` 防止削顶，或检查 WiFi 信号质量。

---

## 10. 常见问题

**Q1：OLED 一直显示 `WiFi : Offline`？**
检查 `src/secrets.h` 中的 WiFi 名称和密码；确认是 2.4 GHz 网络；串口会打印 `WiFi connect failed` 和重连日志。

**Q2：烧录成功但串口没有任何输出？**
确认监视器波特率是 `115200`；确认打开的是正确的 COM 口；按一下开发板的 `RST` 键。

**Q3：OLED 不亮或花屏？**
先在串口看是否卡在初始化；确认 I2C 地址是 `0x3C`，部分模块是 `0x3D`，可修改 `config.h` 中的 `OLED_ADDR`；检查 SDA/SCL 是否接反。

**Q4：两台设备都连上网，但互相听不到？**
- 局域网模式：确认在同一个路由器、没有开启 AP 隔离。
- 中继模式：确认服务器进程在运行、云安全组和 ufw 都放行了 **UDP** 4210、设备串口打印了 `Relay resolved`。
- 两台设备不要使用相同的 `deviceId`（正常情况下由 MAC 生成，不会重复）。

**Q5：只有一边能听到，另一边没反应？**
先确认没反应那台的串口是否出现 `UDP send failed`；再检查它的麦克风接线（尤其 L/R 是否接地），必要时把 `MIC_USE_RIGHT_CHANNEL` 改为 1。

**Q6：录音全是静音？**
确认 INMP441 的 `L/R` 接 GND，`VDD` 接 3.3 V，`SD` 接 GPIO33；确认麦克风没有被面包板接触不良。

**Q7：声音断断续续、有咔哒声？**
发送端静音时不上行，接收端 300 ms 超时会重建 I2S 驱动，暂停说话时就会出现切换声。可以降低 `VOICE_SEND_THRESHOLD` 让静音也少量发包，或修改 `main.cpp` 中的 300 ms 超时。

**Q8：接上喇叭后有持续杂音？**
MAX98357A 的 `DIN` 在录音期间处于浮空状态，可能拾取干扰。可在 `DIN` 与 GND 之间加一个 10 kΩ 下拉电阻，或给功放单独供电。

**Q9：开发板反复重启？**
通常是供电不足。换用质量更好的 USB 线/接口，或给功放单独供电并共地。

**Q10：换了一台设备，需要改 `deviceId` 吗？**
不需要。`deviceId` 由 MAC 地址自动生成，每块板子不同。

---

## 11. 目录结构

```
├── platformio.ini          # PlatformIO 工程配置（开发板、依赖库）
├── README.md               # 本文档
├── LICENSE                 # MIT 许可证
├── src/
│   ├── main.cpp            # 主循环与状态机
│   ├── config.h            # 引脚、音频参数、网络模式
│   ├── secrets.example.h   # 私有配置模板（WiFi、中继地址）
│   ├── audio.cpp/.h        # I2S 录音与放音
│   ├── network.cpp/.h      # WiFi、UDP 收发、中继
│   ├── oled.cpp/.h         # OLED 界面
│   └── button.cpp/.h       # 按键封装
└── server/
    └── relay-server.js     # 公网 UDP 中继服务
```

> `src/secrets.h` 需要你自己创建，已被 `.gitignore` 排除；实验报告生成脚本和 docx 产物也仅在本地保留，不属于本仓库内容。

---

## 12. 许可证

本项目使用 [MIT License](LICENSE)，可自由使用、修改、分发，包括商业用途，但需保留版权声明。作者不对使用过程中造成的任何损失负责。
