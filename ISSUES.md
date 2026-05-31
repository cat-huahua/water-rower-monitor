# 已知问题与改进建议 / Issues

针对水阻划船机（water-resistance rower）固件的分析。核心结论：**距离常数量级错误**导致距离严重偏小；**拉桨/SPM 检测模型**（靠"脉冲停 500ms"切桨）对水阻机不成立。

机器参数（用于物理估算）：水箱 D≈0.5 m，水≈17 kg，桨每转 60 脉冲。

| # | 标题 | 类型 | 优先级 |
|---|------|------|--------|
| 1 | `METERS_PER_PULSE` 量级错误，距离读数偏小约 60–120× | bug | P0 |
| 2 | 标定公式自指错误 + 文档常数与代码差 60× | bug/docs | P0 |
| 3 | 拉桨次数：用"脉冲停 500ms"切桨，水阻机飞轮不停 → 整次只计 1 桨 | bug | P0 |
| 4 | SPM 是单拍瞬时值 + 基于错误桨边界 → 乱跳不准 | bug | P1 |
| 5 | 瞬时速度 / 500m split 由单个脉冲间隔算，噪声大 | bug | P1 |
| 6 | LDR 轮询采样会漏脉冲（loop 阻塞）→ 距离偏小 | bug | P1 |
| 7 | 用 recovery 段估算有效阻力系数 k_eff，做慢速自标定 | enhancement | P2 |
| 8 | `CAL_PER_METER` 死常量，与实际卡路里公式不一致 | cleanup | P2 |
| 9 | BLE 总距离字段 16-bit，>65535 m 回绕 | bug | P3 |
| 10 | `config.h.example` 缺 CALIB_* 宏（全新 clone 编译失败）；admin 默认免密 | bug/feat | P1 |

---

## #1 — `METERS_PER_PULSE` 量级错误，距离读数偏小约 60–120× — P0

**位置**
- [`firmware/include/config.h.example:87`](firmware/include/config.h.example#L87) — `#define METERS_PER_PULSE (0.00875f / 60)` ≈ `0.0001458`
- [`firmware/src/main.cpp:1318`](firmware/src/main.cpp#L1318) — `totalMeters += newPulses * calMetersPerPulse;`
- [`firmware/src/main.cpp:1205`](firmware/src/main.cpp#L1205) — 校准屏把它当"每圈米数"显示：`calMetersPerPulse * PULSES_PER_REV` = 0.00875 m/圈

**现象**
连续划行后距离显示远小于真实值（30 分钟可能只显示几十米）。

**根因（物理估算）**
距离模型本身（线性累加 `每脉冲 × 常数`）在稳态下成立，错的是常数量级：
- 桨尖速度只能是几 m/s 量级 → 桨 ≈ 2–4 转/秒（120–240 RPM）。
- 由例子数据反推（船速 3.6 m/s ÷ ~2.5 转/秒）→ **每圈 ≈ 1.4 m，每脉冲 ≈ 0.024 m**。
- 合理区间：每脉冲 **0.015–0.03**。
- 代码现值 0.000146 → **小约 60–120×**。

**建议修复**
1. 把口径统一为"每圈米数"，只在一处明确 `÷ PULSES_PER_REV`，并加注释：
   ```c
   // 标定目标：每圈（60 脉冲）行进米数，需在真机上校准
   #define METERS_PER_REV   1.4f                      // ← 标定量（默认估算值）
   #define METERS_PER_PULSE (METERS_PER_REV / PULSES_PER_REV)
   ```
2. 默认值先置 ~0.025 m/pulse（1.4–1.5 m/圈），上机标定后微调。

**验收标准**
划一段已知/可对比距离，显示距离与真实在 ±10% 内。

---

## #2 — 标定公式自指错误 + 文档常数与代码差 60× — P0

**位置**
- [`APPLY.md:358`](APPLY.md#L358) / [`WIRING.md:156`](WIRING.md#L156) — `新值 = 实际距离 / 屏幕显示距离 × 0.00875`
- [`WIRING.md:131`](WIRING.md#L131) — 文档把 `METERS_PER_PULSE` 默认值写成 `0.00875`，但代码是 `0.00875/60`（差 60×）

**现象**
用户照文档标定，算出的值再差约 60×，越标越错。

**根因**
1. 公式把 `0.00875` 写死，只有当前值恰好等于它时才对；当前实际是 0.000146。
2. 文档没区分"每脉冲" vs "每圈"，与代码口径不一致。

**建议修复**
- 正确公式（与当前值无关）：
  ```
  新 METERS_PER_PULSE = 当前 METERS_PER_PULSE × (实际距离 / 屏幕显示距离)
  ```
- 文档统一术语：明确标注是"每脉冲"还是"每圈"，与 #1 的命名对齐。
- `CALIBRATION` 屏（[`main.cpp:1188`](firmware/src/main.cpp#L1188)）可直接显示"每圈米数"供用户对照。

---

## #3 — 拉桨次数检测模型错误（水阻机飞轮不停）— P0

**位置**
- [`firmware/src/main.cpp:1327-1345`](firmware/src/main.cpp#L1327) — `inStroke` 逻辑
- [`firmware/include/config.h.example:88`](firmware/include/config.h.example#L88) — `STROKE_GAP_MS 500`

**现象**
连续划行时拉桨次数几乎不增长（常停在 1）。

**根因**
现逻辑用"脉冲间隔 > 500 ms 才算一桨结束"切分。但水阻机回桨段水储能、桨叶持续旋转，脉冲不会停 500 ms → `inStroke` 永不复位 → 整次只 `strokeCount++` 一次。判据把"飞轮转/停"误当成了"drive/recovery"。

**建议修复**
改用**角加速度（ω 斜率）**切分一桨：
- 脉冲间隔变短（ω↑）= drive 开始 → `strokeCount++`；
- 脉冲间隔变长（ω↓）= recovery。
- 加 EMA 平滑 ω、滞回（~3–5%）与最小桨时（~600 ms，对应上限 ~100 SPM）防误计；空闲衰减。
- `STROKE_GAP_MS` 退役。

**验收标准**
慢/快划各 N 桨，计数误差 ≤ ±1 桨/分钟。

---

## #4 — SPM 单拍瞬时 + 基于错误桨边界 — P1

**位置**
- [`firmware/src/main.cpp:1330-1336`](firmware/src/main.cpp#L1330) — `strokeRate = 60000.0f / strokeInterval;`

**现象**
SPM 数字跳变剧烈、读数不可信。

**根因**
建立在 #3 的错误桨边界之上，且只取最近一次间隔（瞬时值），无平滑。

**建议修复**
- **≥10s 滑动窗口平均**：记录每桨时间戳，回溯到跨度 ≥10s，`SPM = 区间数 / 跨度 × 60`（真正的 ≥10s 平均，无固定窗量化跳变）；
- 空闲衰减到 0，并清空时间戳环，避免跨暂停误算。

依赖 #3。

---

## #5 — 瞬时速度 / 500m split 噪声大 — P1

**位置**
- [`firmware/src/main.cpp:1322-1325`](firmware/src/main.cpp#L1322) — `currentSpeed = calMetersPerPulse / (lastPulseIntervalMs/1000)`
- [`firmware/src/main.cpp:866-875`](firmware/src/main.cpp#L866) — split 由 currentSpeed 推

**现象**
速度与 /500m split 每个脉冲都跳。

**根因**
由单个脉冲间隔直接算，未平滑。

**建议修复**
对 ω（或间隔）做 EMA 后再算速度；split 随之稳定。

---

## #6 — LDR 轮询漏脉冲（距离偏小）— P1

**位置**
- [`firmware/src/main.cpp:433`](firmware/src/main.cpp#L433) — LDR 在 `loop()` 里轮询
- 阻塞源：[`playTone()` 的 `delay()`](firmware/src/main.cpp#L413)、250 ms 全屏重绘（[`REDRAW_MS`](firmware/src/main.cpp#L1524)）、上传阻塞

**现象**
仅 LDR 模式：高转速时距离偏小（漏计脉冲）。Hall / Blocker 走中断不受影响。

**修复（已做）**
- LDR 改用 **1 kHz 硬件定时器 ISR** 采样（`ldrSampleISR`）：ISR 把脉冲计入 volatile 计数器，`readSensor()` 在 loop 里只做拷贝——与 Hall/Blocker 同构。即使 250ms 重绘阻塞 loop，脉冲也不丢，距离精确。
- ISR 只在 `workoutActive` 时读 ADC（此时无 flash/NVS 写）→ ISR 内 `analogRead` 安全；采样率 1 kHz 可调。
- ⚠️ 仅编译验证，需上机确认无崩溃 / 不漏脉冲。

---

## #7 — 用 recovery 段估算有效阻力系数 k_eff（自标定）— P2 enhancement

**背景物理**
撤力后仅阻力作用：`I·dω/dt = −k·ω²` → **`1/ω` 随时间线性增长，斜率 = k/I**；等价于"脉冲间隔随时间线性变大"。
- 估计量：recovery 段对 `(1/ω, t)` 线性回归取斜率，`k = I × 斜率`，`I ≈ 0.1 kg·m²`（17 kg 刚体上限 0.53，打滑后约 1/5）。
- **水阻机注意**：水会跟着转，阻力 ∝ `(ω_桨 − ω_水)²`，`(1/ω, t)` 会上凸 → 只在 recovery **前中段固定窗口**拟合，得到的是可复现的"有效 k"，非纯物理值。

**用途（水阻机版定位）**
- 因水量固定、k 稳定，**不建议逐桨动态测**；
- 用作：① drive/recovery 判据（顺带支撑 #3/#4）；② 多桨中位数/慢平均的**自标定**，水位变了能跟。
- 距离**绝对刻度**仍需一次"划已知距离"标定（水阻机无 Concept2 magic 约定，光有 k 定不了绝对米数）。

**决定：暂不实现（非缺陷）。** 水阻机水涡流让逐桨 k 估计噪声大，自动喂距离可能更差；且绝对刻度仍要一次已知距离标定。保留为手动标定；未来如需，可只做"观察用" k_eff 显示，不参与距离。

---

## #8 — `CAL_PER_METER` 死常量 — P2 cleanup

**位置**
- [`firmware/include/config.h.example:89`](firmware/include/config.h.example#L89) — `#define CAL_PER_METER 0.04f`（定义但未使用）
- [`firmware/src/main.cpp:1320`](firmware/src/main.cpp#L1320) — 实际用 `totalMeters * 体重 * 0.000571f`
- [`WIRING.md:133`](WIRING.md#L133) — 文档仍列 `CAL_PER_METER`

**建议修复**
删除 `CAL_PER_METER` 与文档对应行，或统一卡路里口径（二选一），消除歧义。

---

## #9 — BLE 总距离字段 16-bit，>65535 m 回绕 — P3

**位置**
- [`firmware/src/main.cpp:570`](firmware/src/main.cpp#L570) — `uint16_t totalDist = (uint16_t)totalMeters;`（高字节写 0）

**现象**
FTMS 总距离是 uint24，但代码高字节恒 0，距离超 65.5 km 回绕（实际单次训练通常不触发）。

**修复（已做）**
`totalDist` 改 `uint32_t`，写入第三字节 `(totalDist >> 16) & 0xFF`。

---

## #10 — config.h.example 缺 CALIB 宏 + admin 默认免密 — P1

**位置** [`config.h.example`](firmware/include/config.h.example)（原先缺 `CALIB_COMBO/_LEN`、`CALIB_PASSWORD/_LEN`）

**现象** admin/calib 功能的提交加了代码但没更新模板 → **全新 clone 编译失败**。

**修复（已做）**
- 补全 4 个 CALIB 宏；
- 新增开关 `CALIB_REQUIRE_PASSWORD`，**默认 `0`（不要密码）** —— 暗号组合直接进 admin 菜单；置 `1` 才要密码。
- `main.cpp` 用 `#if CALIB_REQUIRE_PASSWORD` 决定走密码屏还是直进菜单；未定义时按 0 处理（旧 config.h 自动免密）。

---

## 实施建议（顺序）

1. **P0 一批**：#1 常数置 0.025 + #2 标定公式 + #3 ω 斜率数桨 —— 一次改 `processSensor()` + `config.h` + 文档。
2. **P1**：#4 SPM 平滑、#5 速度平滑（与 #3 共用 ω 平滑机制）、#6 LDR 中断化。
3. **P2**：#7 k_eff 自标定（先只显示观察）、#8 清理。
