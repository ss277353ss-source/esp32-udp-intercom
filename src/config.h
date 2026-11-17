#ifndef CONFIG_H
#define CONFIG_H

// OLED SSD1306 I2C
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDR 0x3C

// PTT button, active LOW with INPUT_PULLUP
#define BUTTON_PIN 18

// 私有配置（WiFi 账号密码、中继服务器地址）放在 secrets.h 中，不提交到仓库。
// 首次使用请复制 secrets.example.h 为 secrets.h 并填写真实值。
#if defined(__has_include)
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "未找到 src/secrets.h，已回退到 secrets.example.h 的占位配置，WiFi 将无法连接"
#include "secrets.example.h"
#endif
#else
#include "secrets.h"
#endif

// UDP audio broadcast
#define AUDIO_UDP_PORT 4210

// Network mode:
// 1 = same-WiFi LAN broadcast
// 2 = public relay server
#define NETWORK_MODE_LAN 1
#define NETWORK_MODE_RELAY 2
#define NETWORK_MODE NETWORK_MODE_RELAY

// Public UDP relay server（RELAY_HOST / RELAY_PORT 定义在 secrets.h）
#define RELAY_KEEPALIVE_MS 1000

// INMP441 microphone
#define MIC_BCLK 26
#define MIC_WS 25
#define MIC_SD 33

// MAX98357A amplifier
#define SPK_BCLK 26
#define SPK_WS 25
#define SPK_DIN 32

// Audio settings
#define SAMPLE_RATE 16000
#define AUDIO_BLOCK_SIZE 512
#define STREAM_GAIN 2
#define AUDIO_NOISE_GATE 260
#define VOICE_SEND_THRESHOLD 650
#define SPEAKER_VOLUME_PERCENT 20

// INMP441: L/R tied to GND usually means left channel.
// Change this to 1 only if the microphone becomes silent.
#define MIC_USE_RIGHT_CHANNEL 0

#endif
