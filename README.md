# Water Rower Monitor

ESP32-S3 replacement console for WaterRower machines. The original monitor was destroyed by leaking batteries — this project replaces it with modern hardware and adds WiFi workout logging.

## Features

- Real-time display: time, distance, split/500m, stroke rate, calories
- 1.8" color TFT screen (ST7735S 128x160) with 4 built-in keys
- LDR sensor reads the original WaterRower optical sensor (no modifications needed)
- Speaker for audio feedback (start/stop/button sounds)
- Automatic workout upload to a private GitHub repo as JSON
- Workout history browsing (last 10 sessions)
- Adjustable screen brightness
- Powered by 5V USB-C battery pack (3000mAh)
- 3D printable enclosure with slide-in battery door

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-S3 DevKitC-1 |
| Display | ST7735S 1.8" 128x160 RGB TFT (with 4 keys) |
| Sensor | Original WaterRower LDR (3-wire: VCC, Signal, GND) |
| Audio | Small oval speaker (PWM driven) |
| Power | 5V 3000mAh 18650 USB-C battery pack |
| Board | Large half breadboard (165x55mm) |
| Mount | 18mm tube socket (fits original WaterRower arm) |

## Wiring

See [WIRING.md](WIRING.md) for full wiring diagram and pin assignments.

## Assembly

See [APPLY.md](APPLY.md) for step-by-step assembly guide with photos.

## Setup

1. Copy the config template:
   ```bash
   cp firmware/include/config.h.example firmware/include/config.h
   ```

2. Edit `firmware/include/config.h` with your:
   - WiFi SSID and password
   - GitHub personal access token and repo info

3. Create a **private** GitHub repo for workout logs, with a `workouts/` folder

4. Flash the firmware:
   ```bash
   cd firmware
   pio run -t upload
   ```

5. Export STL files from `enclosure/enclosure.scad` using OpenSCAD (F6 → F7)

## 3D Printed Parts

| Part | File | Description |
|------|------|-------------|
| Bottom case | `bottom_case.stl` | Holds breadboard, battery, speaker |
| Lid | `lid.stl` | TFT window + engraved label |
| Tube socket | `tube_socket.stl` | 18mm mount for WaterRower arm |
| Battery door | `battery_door.stl` | Slide-in cap for battery swap |

## Controls

| Key | Idle | Rowing | Paused |
|-----|------|--------|--------|
| * | Start | — | Resume |
| # | History | Pause | End + Upload |
| UP | Brightness+ | — | — |
| DOWN | Brightness- | — | — |

## Workout Data

Each workout is saved as JSON to your private GitHub repo:

```json
{
  "machine_sn": "132224",
  "date": "2026-04-02T18:30:25Z",
  "duration_sec": 1800,
  "distance_m": 6500,
  "strokes": 450,
  "calories": 260,
  "split_500m": "2:18"
}
```

## Calibration

1. Power on — idle screen shows live LDR value
2. Set `LDR_THRESHOLD` to midpoint between light/dark readings
3. Row 10 strokes, compare displayed distance to actual
4. Adjust `METERS_PER_PULSE` accordingly

## License

[MIT](LICENSE)
