#!/usr/bin/env python3
"""Generate breadboard top-view wiring SVG for Water Rower Monitor."""

W, H = 1100, 680

lines = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">']
lines.append('<rect width="100%" height="100%" fill="#1a1a2e"/>')
lines.append('<style>text{font-family:monospace;} .lbl{font-size:11px;fill:#ccc;} .pin{font-size:9px;} .title{font-size:13px;font-weight:bold;}</style>')

def rect(x, y, w, h, fill, stroke="#555", rx=4, sw=1.5, opacity=1.0):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" rx="{rx}" opacity="{opacity}"/>'

def text(x, y, s, cls="lbl", anchor="middle", fill=None):
    f = f' fill="{fill}"' if fill else ""
    return f'<text x="{x}" y="{y}" text-anchor="{anchor}" class="{cls}"{f}>{s}</text>'

def wire(x1, y1, x2, y2, color, w=2.2):
    return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="{w}" stroke-linecap="round"/>'

def wire_path(pts, color, w=2.2):
    d = "M " + " L ".join(f"{x},{y}" for x, y in pts)
    return f'<path d="{d}" stroke="{color}" stroke-width="{w}" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'

def dot(x, y, color, r=4):
    return f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>'

# ── Breadboard ──────────────────────────────────────────────────────────
BB_X, BB_Y, BB_W, BB_H = 140, 230, 680, 200
RAIL_H = 18
GAP_Y = BB_Y + BB_H // 2 - 6
GAP_H = 12

lines.append(rect(BB_X, BB_Y, BB_W, BB_H, "#2d5a27", "#4a4", rx=6, sw=2))
# top half rows
lines.append(rect(BB_X+8, BB_Y+RAIL_H+4, BB_W-16, BB_H//2-RAIL_H-10, "#3a3a3a", "#555", rx=2))
# bottom half rows
lines.append(rect(BB_X+8, GAP_Y+GAP_H+2, BB_W-16, BB_H//2-RAIL_H-10, "#3a3a3a", "#555", rx=2))
# power rails top
lines.append(rect(BB_X+8, BB_Y+3, BB_W-16, RAIL_H, "#2a2a2a", "#444", rx=2))
# power rails bottom
lines.append(rect(BB_X+8, BB_Y+BB_H-RAIL_H-3, BB_W-16, RAIL_H, "#2a2a2a", "#444", rx=2))

# rail stripes
for i in range(0, BB_W-20, 16):
    lines.append(f'<line x1="{BB_X+10+i}" y1="{BB_Y+5}" x2="{BB_X+10+i}" y2="{BB_Y+RAIL_H}" stroke="#c00" stroke-width="1.5" opacity="0.7"/>')
    lines.append(f'<line x1="{BB_X+10+i}" y1="{BB_Y+BB_H-RAIL_H}" x2="{BB_X+10+i}" y2="{BB_Y+BB_H-5}" stroke="#333" stroke-width="1.5" opacity="0.7"/>')
lines.append(text(BB_X-12, BB_Y+RAIL_H, "+5V", "lbl", "end", "#f55"))
lines.append(text(BB_X-12, BB_Y+BB_H-6, "GND", "lbl", "end", "#888"))

# breadboard label
lines.append(text(BB_X+BB_W//2, BB_Y+BB_H+16, "830-hole Breadboard (165×55mm)", "lbl", "middle", "#888"))

# ── ESP32-S3 DevKitC-1 ─────────────────────────────────────────────────
ESP_X, ESP_Y = 330, 222
ESP_W, ESP_H = 310, 218
lines.append(rect(ESP_X, ESP_Y, ESP_W, ESP_H, "#1a3a5c", "#4af", rx=5, sw=2))
lines.append(text(ESP_X+ESP_W//2, ESP_Y+14, "ESP32-S3 DevKitC-1", "title", "middle", "#4af"))

# USB at top
lines.append(rect(ESP_X+ESP_W//2-18, ESP_Y-10, 36, 14, "#555", "#888", rx=3))
lines.append(text(ESP_X+ESP_W//2, ESP_Y-2, "USB-C", "lbl", "middle", "#aaa"))

# left-side pins (ESP, from top)
LEFT_PINS = [
    ("3V3",    "#f88", True),
    ("GND",    "#888", True),
    ("5V(Vin)","#f55", True),
    ("GPIO16", "#fa0", False),  # Speaker
    ("GPIO15", "#8f8", False),  # K4
    ("GPIO7",  "#8f8", False),  # K3
    ("GPIO6",  "#8f8", False),  # K2
    ("GPIO5",  "#ff0", False),  # K1 + sensor
]
RIGHT_PINS = [
    ("GND",    "#888", True),
    ("GPIO13", "#09f", False),  # BL
    ("GPIO12", "#09f", False),  # SCLK
    ("GPIO11", "#09f", False),  # MOSI
    ("GPIO10", "#09f", False),  # CS
    ("GPIO9",  "#09f", False),  # RST
    ("GPIO8",  "#09f", False),  # DC
    ("3V3",    "#f88", True),
]

PIN_START_Y = ESP_Y + 34
PIN_STEP = 20

for i, (name, color, is_pwr) in enumerate(LEFT_PINS):
    py = PIN_START_Y + i * PIN_STEP
    lines.append(f'<circle cx="{ESP_X+6}" cy="{py}" r="4" fill="{color}" stroke="#000" stroke-width="1"/>')
    lines.append(text(ESP_X+14, py+4, name, "pin", "start", color))

for i, (name, color, is_pwr) in enumerate(RIGHT_PINS):
    py = PIN_START_Y + i * PIN_STEP
    lines.append(f'<circle cx="{ESP_X+ESP_W-6}" cy="{py}" r="4" fill="{color}" stroke="#000" stroke-width="1"/>')
    lines.append(text(ESP_X+ESP_W-14, py+4, name, "pin", "end", color))

# ── TFT ST7735S module ──────────────────────────────────────────────────
TFT_X, TFT_Y = 840, 60
TFT_W, TFT_H = 130, 220
lines.append(rect(TFT_X, TFT_Y, TFT_W, TFT_H, "#1a1a4a", "#66f", rx=5, sw=2))
lines.append(text(TFT_X+TFT_W//2, TFT_Y+14, "ST7735S TFT", "title", "middle", "#66f"))
lines.append(text(TFT_X+TFT_W//2, TFT_Y+26, "1.8\" 128×160", "lbl", "middle", "#aaa"))

# TFT screen area
lines.append(rect(TFT_X+15, TFT_Y+34, TFT_W-30, 90, "#000", "#44f", rx=2))
lines.append(text(TFT_X+TFT_W//2, TFT_Y+85, "SCREEN", "lbl", "middle", "#33f"))

# TFT buttons
BTN_Y = TFT_Y + 134
for i, (lbl, clr) in enumerate([("K1","#8f8"),("K2","#8f8"),("K3","#8f8"),("K4","#8f8")]):
    bx = TFT_X + 8 + i * 29
    lines.append(rect(bx, BTN_Y, 24, 14, "#333", clr, rx=3))
    lines.append(text(bx+12, BTN_Y+10, lbl, "pin", "middle", clr))

# TFT pin list
TFT_PINS = ["GND","VCC","SCL","SDA","RST","DC","CS","BL","K1","K2","K3","K4"]
TFT_COLORS = ["#888","#f88","#09f","#09f","#09f","#09f","#09f","#09f","#8f8","#8f8","#8f8","#8f8"]
for i, (pn, pc) in enumerate(zip(TFT_PINS, TFT_COLORS)):
    py = TFT_Y + 160 + i * 0  # won't fit, show as label block
    pass

lines.append(text(TFT_X+TFT_W//2, TFT_Y+168, "GND VCC SCL SDA", "pin", "middle", "#ccc"))
lines.append(text(TFT_X+TFT_W//2, TFT_Y+180, "RST  DC  CS  BL", "pin", "middle", "#ccc"))
lines.append(text(TFT_X+TFT_W//2, TFT_Y+192, "K1  K2  K3  K4", "pin", "middle", "#ccc"))

# left connector dots on TFT
TFT_CONN = [
    ("GND", "#888"), ("3V3","#f88"),
    ("SCL","#09f"), ("SDA","#09f"), ("RST","#09f"),
    ("DC","#09f"),  ("CS","#09f"),  ("BL","#09f"),
    ("K1","#ff0"),  ("K2","#8f8"),  ("K3","#8f8"), ("K4","#8f8"),
]
for i, (pn, pc) in enumerate(TFT_CONN):
    cy = TFT_Y + 44 + i * 12
    if cy < TFT_Y + TFT_H - 5:
        lines.append(f'<circle cx="{TFT_X}" cy="{cy}" r="3.5" fill="{pc}" stroke="#000" stroke-width="1"/>')

# ── HW-487 Sensor ────────────────────────────────────────────────────────
SEN_X, SEN_Y = 860, 360
SEN_W, SEN_H = 90, 65
lines.append(rect(SEN_X, SEN_Y, SEN_W, SEN_H, "#2a1a0a", "#fa0", rx=5, sw=2))
lines.append(text(SEN_X+SEN_W//2, SEN_Y+13, "HW-487", "title", "middle", "#fa0"))
lines.append(text(SEN_X+SEN_W//2, SEN_Y+25, "OPB365T55", "lbl", "middle", "#888"))
lines.append(text(SEN_X+SEN_W//2, SEN_Y+37, "replacement", "lbl", "middle", "#888"))
SEN_PINS = [("VCC","#f55"),("GND","#888"),("SIG","#ff0")]
for i,(pn,pc) in enumerate(SEN_PINS):
    cy = SEN_Y + 48
    cx = SEN_X + 12 + i*30
    lines.append(dot(cx, cy, pc, 4))
    lines.append(text(cx, cy+13, pn, "pin", "middle", pc))

# ── Speaker ───────────────────────────────────────────────────────────────
SPK_X, SPK_Y = 30, 350
SPK_R = 38
cx, cy = SPK_X+SPK_R, SPK_Y+SPK_R
lines.append(f'<circle cx="{cx}" cy="{cy}" r="{SPK_R}" fill="#1a1a1a" stroke="#fa0" stroke-width="2"/>')
lines.append(f'<circle cx="{cx}" cy="{cy}" r="{SPK_R-8}" fill="none" stroke="#444" stroke-width="1" stroke-dasharray="4,3"/>')
lines.append(f'<circle cx="{cx}" cy="{cy}" r="{SPK_R-18}" fill="none" stroke="#444" stroke-width="1" stroke-dasharray="4,3"/>')
lines.append(text(cx, cy+4, "SPK", "title", "middle", "#fa0"))
lines.append(text(cx, SPK_Y+SPK_R*2+14, "18mm Speaker", "lbl", "middle", "#fa0"))
lines.append(dot(cx-12, SPK_Y+SPK_R*2+4, "#f55", 4))
lines.append(dot(cx+12, SPK_Y+SPK_R*2+4, "#888", 4))
lines.append(text(cx-12, SPK_Y+SPK_R*2+18, "+", "pin", "middle", "#f55"))
lines.append(text(cx+12, SPK_Y+SPK_R*2+18, "-", "pin", "middle", "#888"))

# ── Battery ────────────────────────────────────────────────────────────────
BAT_X, BAT_Y = 340, 488
BAT_W, BAT_H = 160, 50
lines.append(rect(BAT_X, BAT_Y, BAT_W, BAT_H, "#1a2a1a", "#4f4", rx=8, sw=2))
lines.append(text(BAT_X+BAT_W//2, BAT_Y+20, "5V 3000mAh USB-C", "title", "middle", "#4f4"))
lines.append(text(BAT_X+BAT_W//2, BAT_Y+36, "Battery Pack", "lbl", "middle", "#aaa"))
# USB line to ESP32
lines.append(wire_path([(BAT_X+BAT_W//2, BAT_Y), (BAT_X+BAT_W//2, ESP_Y+ESP_H+20), (ESP_X+ESP_W//2, ESP_Y+ESP_H+20), (ESP_X+ESP_W//2, ESP_Y+ESP_H)], "#4f4", 2))
lines.append(text(ESP_X+ESP_W//2+5, ESP_Y+ESP_H+16, "USB-C", "pin", "start", "#4f4"))

# ── WIRES ──────────────────────────────────────────────────────────────────
# ESP left-side pin X coords
LESP_X = ESP_X + 6
RESP_X = ESP_X + ESP_W - 6

def lpin_y(i): return PIN_START_Y + i * PIN_STEP
def rpin_y(i): return PIN_START_Y + i * PIN_STEP

# ── TFT wires (right side of ESP → left side of TFT) ──
# right pins: GND(0), GPIO13-BL(1), GPIO12-SCLK(2), GPIO11-MOSI(3), GPIO10-CS(4), GPIO9-RST(5), GPIO8-DC(6), 3V3(7)
TFT_WIRE_PAIRS = [
    # (esp_right_pin_idx, tft_conn_idx, color, label)
    (7, 1, "#f88",  "3V3→VCC"),
    (0, 0, "#888",  "GND"),
    (6, 5, "#09f",  "GPIO8→DC"),
    (5, 4, "#09f",  "GPIO9→RST"),
    (4, 6, "#09f",  "GPIO10→CS"),
    (3, 2, "#09f",  "GPIO11→SDA"),
    (2, 3, "#09f",  "GPIO12→SCL"),
    (1, 7, "#09f",  "GPIO13→BL"),
]

ROUTE_X = TFT_X - 20  # vertical routing lane

for esp_i, tft_i, color, lbl in TFT_WIRE_PAIRS:
    ey = rpin_y(esp_i)
    ty = TFT_Y + 44 + tft_i * 12
    # route: ESP right → routing lane → TFT left
    lines.append(wire_path([
        (RESP_X, ey),
        (ROUTE_X, ey),
        (ROUTE_X, ty),
        (TFT_X, ty)
    ], color, 1.8))

# ── K1(GPIO5) left side → TFT K1 and HW-487 SIG ──
# GPIO5 = left pin index 7
GPIO5_Y = lpin_y(7)
# TFT K1 conn index = 8
TFT_K1_Y = TFT_Y + 44 + 8 * 12
# route ESP left → right then up to TFT
# First go to HW-487 SIG
SEN_SIG_X = SEN_X + 12
SEN_SIG_Y = SEN_Y + 48
lines.append(wire_path([
    (LESP_X, GPIO5_Y),
    (LESP_X - 20, GPIO5_Y),
    (LESP_X - 20, SEN_SIG_Y),
    (SEN_SIG_X, SEN_SIG_Y)
], "#ff0", 2))

# K2 GPIO6 → TFT K2 (left pin 6)
GPIO6_Y = lpin_y(6)
TFT_K2_Y = TFT_Y + 44 + 9 * 12
lines.append(wire_path([
    (LESP_X, GPIO6_Y),
    (LESP_X-30, GPIO6_Y),
    (LESP_X-30, TFT_K2_Y),
    (ROUTE_X, TFT_K2_Y),
    (TFT_X, TFT_K2_Y)
], "#8f8", 1.8))

# K3 GPIO7 (left pin 5)
GPIO7_Y = lpin_y(5)
TFT_K3_Y = TFT_Y + 44 + 10 * 12
lines.append(wire_path([
    (LESP_X, GPIO7_Y),
    (LESP_X-35, GPIO7_Y),
    (LESP_X-35, TFT_K3_Y),
    (ROUTE_X, TFT_K3_Y),
    (TFT_X, TFT_K3_Y)
], "#8f8", 1.8))

# K4 GPIO15 (left pin 4)
GPIO15_Y = lpin_y(4)
TFT_K4_Y = TFT_Y + 44 + 11 * 12
lines.append(wire_path([
    (LESP_X, GPIO15_Y),
    (LESP_X-40, GPIO15_Y),
    (LESP_X-40, TFT_K4_Y),
    (ROUTE_X, TFT_K4_Y),
    (TFT_X, TFT_K4_Y)
], "#8f8", 1.8))

# Speaker GPIO16 (left pin 3)
GPIO16_Y = lpin_y(3)
SPK_PLUS_X = cx - 12
SPK_PLUS_Y = SPK_Y + SPK_R * 2 + 4
lines.append(wire_path([
    (LESP_X, GPIO16_Y),
    (SPK_PLUS_X, GPIO16_Y),
    (SPK_PLUS_X, SPK_PLUS_Y)
], "#fa0", 2))

# GND speaker
SPK_GND_X = cx + 12
lines.append(wire_path([
    (BB_X+8, BB_Y+BB_H-RAIL_H+9),
    (SPK_GND_X, BB_Y+BB_H-RAIL_H+9),
    (SPK_GND_X, SPK_Y + SPK_R*2 + 4)
], "#888", 1.8))

# HW-487 VCC → 5V rail
SEN_VCC_X = SEN_X + 12
SEN_GND_X = SEN_X + 42
lines.append(wire_path([
    (SEN_VCC_X, SEN_Y+48),
    (SEN_VCC_X, SEN_Y+20),
    (BB_X+BB_W-10, SEN_Y+20),
    (BB_X+BB_W-10, BB_Y+10)
], "#f55", 2))
lines.append(text(BB_X+BB_W-30, SEN_Y+17, "5V→HW-487", "pin", "end", "#f55"))

# HW-487 GND
lines.append(wire_path([
    (SEN_GND_X, SEN_Y+48),
    (SEN_GND_X, BB_Y+BB_H-10)
], "#888", 1.8))

# ESP GND → rail
lines.append(wire_path([
    (LESP_X, lpin_y(1)),
    (BB_X+12, lpin_y(1)),
    (BB_X+12, BB_Y+BB_H-10)
], "#888", 1.8))

# ESP 5V → rail
lines.append(wire_path([
    (LESP_X, lpin_y(2)),
    (BB_X+20, lpin_y(2)),
    (BB_X+20, BB_Y+10)
], "#f55", 2))

# 3V3 → TFT VCC (already handled via right pin 7)
# ESP 3V3 left → breadboard (optional power)
lines.append(wire_path([
    (LESP_X, lpin_y(0)),
    (BB_X+30, lpin_y(0)),
], "#f88", 1.5))

# ── Legend ─────────────────────────────────────────────────────────────────
LEG_X, LEG_Y = 20, 50
lines.append(rect(LEG_X, LEG_Y, 280, 155, "#111", "#555", rx=6))
lines.append(text(LEG_X+140, LEG_Y+16, "WIRE LEGEND", "title", "middle", "#fff"))
LEGEND = [
    ("#f55",  "5V / VCC"),
    ("#f88",  "3.3V"),
    ("#888",  "GND"),
    ("#09f",  "SPI (TFT)"),
    ("#ff0",  "GPIO5 — Sensor SIG + K1"),
    ("#8f8",  "Button (K2 K3 K4)"),
    ("#fa0",  "GPIO16 — Speaker"),
    ("#4f4",  "USB-C Power"),
]
for i, (col, lbl) in enumerate(LEGEND):
    lx = LEG_X + 14
    ly = LEG_Y + 32 + i * 15
    lines.append(f'<line x1="{lx}" y1="{ly}" x2="{lx+22}" y2="{ly}" stroke="{col}" stroke-width="3" stroke-linecap="round"/>')
    lines.append(text(lx+28, ly+4, lbl, "lbl", "start", col))

# Title
lines.append(text(W//2, 22, "Water Rower Monitor — Breadboard Wiring (Top View)", "title", "middle", "#fff"))

lines.append('</svg>')

out = "/home/ma-xiuyuan/water-rower-monitor/docs/breadboard_wiring.svg"
with open(out, "w") as f:
    f.write("\n".join(lines))
print(f"Written: {out}")
