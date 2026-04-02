# Water Rower Monitor - 接线图 & 配置指南 (ESP32-S3)

## 接线图

```
                   ┌────────────────────┐
                   │   ESP32-S3 DevKit   │
                   │                    │
                   │  3V3 ─────────────┐│
                   │  GND ───────────┐ ││
                   │                 │ ││
  ┌─ ST7735S TFT ─┤                 │ ││
  │  (含4按键)      │  GPIO 10 (CS) ──┼─┼┼── TFT CS
  │                │  GPIO 8  (DC) ──┼─┼┼── TFT DC/A0
  │                │  GPIO 9  (RST)──┼─┼┼── TFT RST
  │                │  GPIO 11 (MOSI)─┼─┼┼── TFT SDA
  │                │  GPIO 12 (SCLK)─┼─┼┼── TFT SCL
  │                │  GPIO 13 (BL) ──┼─┼┼── TFT LED
  │  TFT 按键:     │                 │ ││
  │  K1(UP)        │  GPIO 5  ──────┼─┼┼── K1
  │  K2(DN)        │  GPIO 6  ──────┼─┼┼── K2
  │  K3(#)         │  GPIO 7  ──────┼─┼┼── K3
  │  K4(*)         │  GPIO 15 ──────┼─┼┼── K4
  │                │                 │ ││
  │                │         3V3 ────┘ ││── TFT VCC
  │                │         GND ──────┘│── TFT GND
  └────────────────┤                    │
                   │                    │
  ┌─ LDR 光敏电阻 ─┤  (原机3线)         │
  │                │  GPIO 1  ── SIG ──┤  (模拟信号)
  │                │  3V3     ── VCC   │
  │                │  GND     ── GND ──┘
  └────────────────┤                │
                   │                │
  ┌─ 喇叭 ────────┤                │
  │                │  GPIO 16 ── + │  (红线)
  │                │  GND     ── - │  (黑线)
  └────────────────┤                │
                   │                │
  ┌─ 电池 ────────┤                │
  │  5V 3000mAh   │  USB-C ← USB-C 电池包
  └────────────────┘────────────────┘
```

## ST7735S 1.8" TFT 引脚对照 (ESP32-S3)

```
TFT 引脚         →   ESP32-S3 引脚
──────────────────────────────────
GND              →   GND
VCC              →   3V3
SCL (SCK/CLK)    →   GPIO 12
SDA (MOSI/DIN)   →   GPIO 11
RST (RES)        →   GPIO 9
DC  (A0/RS)      →   GPIO 8
CS  (SS)         →   GPIO 10
LED (BLK/BL)     →   GPIO 13
K1               →   GPIO 5   (UP)
K2               →   GPIO 6   (DOWN)
K3               →   GPIO 7   (#)
K4               →   GPIO 15  (*)
```

## LDR 光敏电阻 (原机自带, 3线)

```
线缆颜色       →   ESP32-S3
──────────────────────────────
红 (VCC)       →   3V3 电源轨
黑 (GND)       →   GND 地线轨
信号线 (SIG)    →   GPIO 1 (ADC, 模拟读取)
```

## 喇叭

```
喇叭线         →   ESP32-S3
──────────────────────────────
红 (+)         →   GPIO 16 (PWM)
黑 (-)         →   GND
```

## 电源

```
5V 3000mAh 电池包 ──USB-C线──→ ESP32-S3 USB-C 口
```

## 面板布局 (从正面看)

```
┌──────────────────────────────────────────┐
│                                          │
│   WATER     ┌──────────────────────┐     │
│   ROWER     │ [K1] [K2] [K3] [K4] │     │
│             │  UP   DN    #    *   │     │
│             ├──────────────────────┤     │
│             │                      │     │
│             │    1.8" TFT 屏幕     │     │
│             │    128 x 160         │     │
│             │                      │     │
│             └──────────────────────┘     │
│                                          │
└──────────────────────────────────────────┘
  左侧: USB-C口 (烧录)    右侧: LDR线槽口
  背面: 电池盖 (滑入式) + 充电口
```

## 需要修改的变量

### config.h — 必须改

| 变量 | 说明 | 示例 |
|------|------|------|
| `WIFI_SSID` | 你的 WiFi 名称 | `"MyWiFi"` |
| `WIFI_PASSWORD` | WiFi 密码 | `"password123"` |
| `GITHUB_TOKEN` | GitHub Personal Access Token (repo 权限) | `"ghp_xxxx..."` |
| `GITHUB_OWNER` | GitHub 用户名 | `"my-username"` |
| `GITHUB_REPO` | 仓库名 | `"water-rower-logs"` |

### config.h — 校准后调

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `LDR_THRESHOLD` | LDR 脉冲检测阈值 (0-4095) | `2000` |
| `LDR_HYSTERESIS` | 防抖动余量 | `300` |
| `METERS_PER_PULSE` | 每个脉冲对应的米数 | `0.00875f` |
| `STROKE_GAP_MS` | 划桨间隙检测 (ms) | `500` |
| `CAL_PER_METER` | 每米消耗卡路里 | `0.04f` |

### enclosure.scad — 根据实物调

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `tube_od` | 原机金属管外径 | `18mm` (已确认) |
| `tft_pcb_w` | TFT 模块 PCB 总宽 | `52mm` |
| `tft_pcb_l` | TFT 模块 PCB 总高 | `40mm` |
| `tft_screen_w` | 屏幕可视区宽 | `29mm` |
| `tft_screen_l` | 屏幕可视区高 | `37mm` |
| `bat_w` | 电池包长度 | `70mm` |
| `bat_l` | 电池包宽度 | `37mm` |
| `bat_h` | 电池包厚度 | `20mm` |
| `spk_screw_spacing_w` | 喇叭螺丝孔距(宽) | `48mm` |
| `spk_screw_spacing_l` | 喇叭螺丝孔距(高) | `24mm` |

## 校准步骤

1. 开机后空闲页面会显示实时 LDR 值: `LDR: xxxx (thr:2000)`
2. 转动飞轮, 观察值的变化范围
3. 设 `LDR_THRESHOLD` 为亮/暗中间值
4. 按 * 开始训练, 划 10 桨, 记下距离
5. 计算: `新值 = 实际距离 / 屏幕距离 × 0.00875`
6. 改 config.h, 重新烧录
