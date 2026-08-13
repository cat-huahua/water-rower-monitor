/*
 * Water Rower Monitor — Waveshare ESP32-S3-Touch-LCD-2.8
 * Replacement console for Water Rower USA (SN 132224)
 *
 * Hardware:
 *   - Waveshare ESP32-S3-Touch-LCD-2.8 (ESP32-S3R8, 16MB flash, 8MB PSRAM)
 *   - ST7789 2.8" 240x320 IPS, SPI2 — see config.h for the fixed pin map
 *   - CST328 capacitive touch, I2C bus 1 — the only user input on this board
 *   - PCM5101 I2S DAC + amplifier + speaker header (tones, no PWM buzzer)
 *   - Sensor: optical blocker (原机光遮断器) OR hall effect, on 12PIN GPIO18
 *     Choose in config.h: SENSOR_TYPE_BLOCKER or SENSOR_TYPE_HALL
 *
 * Input model:
 *   The board has no keys beyond BOOT/RESET, so every screen is driven by
 *   on-screen touch targets registered through beginBtns()/addBtn(): the same
 *   rectangle is both drawn and hit-tested, so the two can never drift apart.
 *   Admin opens by HOLDING the title bar on the idle screen for 3 s.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <mbedtls/base64.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "config.h"

// Compile-time sensor check
#if !defined(SENSOR_TYPE_LDR) && !defined(SENSOR_TYPE_HALL) && !defined(SENSOR_TYPE_BLOCKER)
  #error "Define SENSOR_TYPE_LDR, SENSOR_TYPE_HALL, or SENSOR_TYPE_BLOCKER in config.h"
#endif
#if (defined(SENSOR_TYPE_LDR) + defined(SENSOR_TYPE_HALL) + defined(SENSOR_TYPE_BLOCKER)) > 1
  #error "Only define ONE sensor type in config.h"
#endif

// ───────── Pixel Art Bitmaps (16x16, 1bpp) ─────────

// Rowing person (stick figure rowing)
static const uint8_t bmp_rower[] PROGMEM = {
    0b00000110, 0b00000000,
    0b00001111, 0b00000000,
    0b00000110, 0b00000000,
    0b00011111, 0b10000000,
    0b00101100, 0b11000000,
    0b01001100, 0b00110000,
    0b00001100, 0b00001000,
    0b00011110, 0b00000000,
    0b00110011, 0b00000000,
    0b01100001, 0b10000000,
    0b00000000, 0b11100000,
    0b00000000, 0b00000000,
    0b01111111, 0b11111110,
    0b11111111, 0b11111111,
    0b01111111, 0b11111110,
    0b00111001, 0b11001100,
};

// Water wave
static const uint8_t bmp_wave[] PROGMEM = {
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000,
    0b00100000, 0b10000010,
    0b01010001, 0b01000101,
    0b10001010, 0b00101000,
    0b00000100, 0b00010000,
    0b00000000, 0b00000000,
    0b00100000, 0b10000010,
    0b01010001, 0b01000101,
    0b10001010, 0b00101000,
};

// Trophy / medal
static const uint8_t bmp_trophy[] PROGMEM = {
    0b00000001, 0b10000000,
    0b00000011, 0b11000000,
    0b00000011, 0b11000000,
    0b00000001, 0b10000000,
    0b00111111, 0b11111100,
    0b01111111, 0b11111110,
    0b01111111, 0b11111110,
    0b01111111, 0b11111110,
    0b00111111, 0b11111100,
    0b00011111, 0b11111000,
    0b00001111, 0b11110000,
    0b00000111, 0b11100000,
    0b00000011, 0b11000000,
    0b00000011, 0b11000000,
    0b00000111, 0b11100000,
    0b00001111, 0b11110000,
};

// Fire / flame
static const uint8_t bmp_fire[] PROGMEM = {
    0b00000001, 0b00000000,
    0b00000011, 0b00000000,
    0b00000011, 0b10000000,
    0b00000111, 0b10000000,
    0b00001111, 0b11000000,
    0b00001111, 0b11000000,
    0b00011111, 0b11100000,
    0b00011111, 0b11100000,
    0b00111111, 0b11110000,
    0b00111101, 0b11110000,
    0b01111100, 0b11111000,
    0b01111000, 0b11111000,
    0b01111000, 0b01111000,
    0b01111100, 0b01111000,
    0b00111111, 0b11110000,
    0b00001111, 0b11000000,
};

// Smiley face
static const uint8_t bmp_happy[] PROGMEM = {
    0b00000111, 0b11100000,
    0b00011111, 0b11111000,
    0b00111111, 0b11111100,
    0b01111111, 0b11111110,
    0b01110011, 0b11001110,
    0b11110011, 0b11001111,
    0b11111111, 0b11111111,
    0b11111111, 0b11111111,
    0b11111111, 0b11111111,
    0b11100111, 0b11100111,
    0b11110000, 0b00001111,
    0b01111000, 0b00011110,
    0b01111111, 0b11111110,
    0b00111111, 0b11111100,
    0b00011111, 0b11111000,
    0b00000111, 0b11100000,
};

// Sad face (upload failed)
static const uint8_t bmp_sad[] PROGMEM = {
    0b00000111, 0b11100000,
    0b00011111, 0b11111000,
    0b00111111, 0b11111100,
    0b01111111, 0b11111110,
    0b01110011, 0b11001110,
    0b11110011, 0b11001111,
    0b11111111, 0b11111111,
    0b11111111, 0b11111111,
    0b11111111, 0b11111111,
    0b11111111, 0b11111111,
    0b11111000, 0b00011111,
    0b11110000, 0b00001111,
    0b01100111, 0b11100110,
    0b00111111, 0b11111100,
    0b00011111, 0b11111000,
    0b00000111, 0b11100000,
};

// Skull (battery low warning)
static const uint8_t bmp_skull[] PROGMEM = {
    0b00000111, 0b11100000,
    0b00011111, 0b11111000,
    0b00111111, 0b11111100,
    0b01111111, 0b11111110,
    0b01110011, 0b11001110,
    0b01100001, 0b10000110,
    0b01110011, 0b11001110,
    0b01111111, 0b11111110,
    0b01111111, 0b11111110,
    0b00111001, 0b10011100,
    0b00011111, 0b11111000,
    0b00001111, 0b11110000,
    0b00001010, 0b10101000,
    0b00001111, 0b11110000,
    0b00001010, 0b10101000,
    0b00001111, 0b11110000,
};

// Funny motivational messages
static const char* const fun_msgs[] = {
    "ROW ROW ROW!",
    "FEEL THE BURN!",
    "KEEP GOING!",
    "YOU GOT THIS!",
    "BEAST MODE!",
    "NO PAIN NO GAIN!",
    "FASTER FASTER!",
    "UNSTOPPABLE!",
    "LIKE A FISH!",
    "WATER WARRIOR!",
};
#define NUM_FUN_MSGS 10

static const char* const idle_msgs[] = {
    "Ready to suffer?",
    "The water awaits...",
    "Don't be lazy!",
    "Let's GOOO!",
    "Calories won't burn themselves",
    "Your muscles miss you",
    "Row or regret?",
    "Just one more session",
};
#define NUM_IDLE_MSGS 8

static const char* const done_msgs[] = {
    "You survived!",
    "Not bad, human!",
    "Fish would be proud",
    "Shower time!",
    "Noodle arms yet?",
    "Champion!",
};
#define NUM_DONE_MSGS 6

// ───────── Colors ─────────
#define COL_BG        ST77XX_BLACK
#define COL_TEXT      ST77XX_WHITE
#define COL_ACCENT    ST77XX_CYAN
#define COL_VALUE     ST77XX_GREEN
#define COL_WARN      ST77XX_YELLOW
#define COL_ERROR     ST77XX_RED
#define COL_LABEL     0x7BEF
#define COL_HEADER_BG 0x000F
#define COL_DIVIDER   0x4208
#define COL_BTN_BG    0x18E3   // idle button fill
#define BLUE          0x1C9F   // water blue

// ───────── Layout (240x320 portrait) ─────────
#define UI_MARGIN     6
#define HEADER_H      32
#define BAR_H         60                       // bottom action bar
#define BAR_Y         (TFT_HEIGHT - BAR_H)
#define BTN_H         52
#define BTN_Y         (BAR_Y + (BAR_H - BTN_H) / 2)
#define CONTENT_Y     (HEADER_H + 4)

// ───────── Display ─────────
// Hardware SPI: the software-SPI constructor used on the old ST7735 build
// bit-bangs ~150 kB per full repaint here, which is seconds per frame.
SPIClass tftSPI(FSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(&tftSPI, TFT_CS, TFT_DC, TFT_RST);
uint8_t brightness = 200;

// ───────── BLE FTMS (Fitness Machine Service) ─────────
// UUIDs per Bluetooth FTMS spec
#define FTMS_SERVICE_UUID        "00001826-0000-1000-8000-00805f9b34fb"
#define ROWER_DATA_UUID          "00002AD1-0000-1000-8000-00805f9b34fb"
#define FTMS_FEATURE_UUID        "00002ACC-0000-1000-8000-00805f9b34fb"
#define FTMS_STATUS_UUID         "00002ADA-0000-1000-8000-00805f9b34fb"

BLEServer* bleServer = nullptr;
BLECharacteristic* rowerDataChar = nullptr;
BLECharacteristic* ftmsFeatureChar = nullptr;
bool bleClientConnected = false;
unsigned long lastBleUpdate = 0;
#define BLE_UPDATE_INTERVAL_MS 500

class FTMSCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* s)    { bleClientConnected = true;  }
    void onDisconnect(BLEServer* s) { bleClientConnected = false;
        BLEDevice::startAdvertising(); }
};

// ───────── Users ─────────
static const char* const userNames[] = USER_NAMES;
int currentUser = 0;
int userScrollOffset = 0;
#define USER_VISIBLE 5   // rows that fit on screen

// ───────── NVS / Calibration ─────────
Preferences prefs;
float calMetersPerPulse = METERS_PER_PULSE;
float userWeights[USER_COUNT + 1];   // +1 for Guest (index USER_COUNT)

void loadCalibration() {
    prefs.begin("wrower", true);
    calMetersPerPulse = prefs.getFloat("mpp", METERS_PER_PULSE);
    // Heal implausible stored values (e.g. old 60x-too-small calibration)
    if (calMetersPerPulse < 0.00005f || calMetersPerPulse > 0.01f)
        calMetersPerPulse = METERS_PER_PULSE;
    for (int i = 0; i <= USER_COUNT; i++) {
        char key[6]; snprintf(key, sizeof(key), "w%d", i);
        userWeights[i] = prefs.getFloat(key, DEFAULT_WEIGHT_KG);
    }
    prefs.end();
}
void saveCalibration() {
    prefs.begin("wrower", false);
    prefs.putFloat("mpp", calMetersPerPulse);
    prefs.end();
}
void saveWeights() {
    prefs.begin("wrower", false);
    for (int i = 0; i <= USER_COUNT; i++) {
        char key[6]; snprintf(key, sizeof(key), "w%d", i);
        prefs.putFloat(key, userWeights[i]);
    }
    prefs.end();
}

// ───────── Admin password ─────────
// Active admin password: config.h value is the factory default; can be
// changed from the admin menu (stored in NVS, key "apwd").
uint8_t adminPwd[CALIB_PASSWORD_LEN] = CALIB_PASSWORD;

void loadAdminPwd() {
    prefs.begin("wrower", true);
    if (prefs.getBytesLength("apwd") == CALIB_PASSWORD_LEN)
        prefs.getBytes("apwd", adminPwd, CALIB_PASSWORD_LEN);
    prefs.end();
}
void saveAdminPwd() {
    prefs.begin("wrower", false);
    prefs.putBytes("apwd", adminPwd, CALIB_PASSWORD_LEN);
    prefs.end();
}

uint8_t pwdBuffer[CALIB_PASSWORD_LEN] = {};
uint8_t pwdPos   = 0;
bool    pwdWrong = false;

// ───────── Admin state ─────────
int adminMenuItem  = 0;   // selected menu item
int adminEditUser  = 0;   // user being edited in weight screen
int adminWeightScroll = 0; // first visible row of the weights list — shared by
                           // the drawing pass and the tap handler so a row tap
                           // always resolves to the user that was drawn there
#define ADMIN_MENU_COUNT 3

// Password edit (double entry to confirm; inactivity timeout = cancel)
uint8_t       pwdNew[CALIB_PASSWORD_LEN] = {};
uint8_t       pwdConfirmPhase = 0;       // 0 = first entry, 1 = re-enter
unsigned long pwdEditLastKeyMs = 0;
#define PWD_EDIT_TIMEOUT_MS 10000

// Hold the title bar on the idle screen to open admin.
#define ADMIN_HOLD_MS 3000

// ───────── State machine ─────────
enum Screen { SCR_IDLE, SCR_USER_SELECT, SCR_WORKOUT, SCR_PAUSED, SCR_SUMMARY,
              SCR_UPLOADING, SCR_HISTORY,
              SCR_CALIB_AUTH, SCR_ADMIN_MENU, SCR_CALIB,
              SCR_ADMIN_WEIGHTS, SCR_ADMIN_WEIGHT_EDIT, SCR_ADMIN_PWD_EDIT };
Screen currentScreen = SCR_IDLE;
int displayPage = 0;

// ───────── Sensor State ─────────
uint32_t pulseCount     = 0;
unsigned long lastPulseMs = 0;
unsigned long lastPulseUsSnap = 0;  // ISR timestamp (us) of the newest accepted pulse

// ω is averaged over a window of OMEGA_AVG_PULSES consecutive pulses (¼ rev).
// Single-pulse intervals carry slot-geometry noise (uneven disc slots) of up to
// ±10-20% at 60x the rev rate — enough to flip the drive/recovery detector even
// past a 15% hysteresis. Averaging across many slots cancels that noise.
#define OMEGA_AVG_PULSES (PULSES_PER_REV / 4)
uint32_t      omegaMarkPulses = 0;  // pulse count at start of current averaging window
unsigned long omegaMarkUs     = 0;  // ISR timestamp at start of window (0 = not seeded)

#ifdef SENSOR_TYPE_LDR
volatile bool          ldrPulseActive     = false;
volatile unsigned long ldrPulseCount      = 0;
volatile unsigned long ldrLastPulseUs     = 0;
volatile unsigned long ldrPulseIntervalUs = 0;
hw_timer_t*            ldrTimer           = nullptr;
#endif

#ifdef SENSOR_TYPE_HALL
volatile unsigned long hallLastPulseUs  = 0;
volatile unsigned long hallPulseCount   = 0;
volatile unsigned long hallPulseIntervalUs = 0;
#endif

#ifdef SENSOR_TYPE_BLOCKER
volatile unsigned long blockerLastPulseUs  = 0;
volatile unsigned long blockerPulseCount   = 0;
volatile unsigned long blockerPulseIntervalUs = 0;
#endif

// ───────── Workout state ─────────
volatile bool workoutActive = false;    // volatile: read by the LDR sampling ISR
uint32_t totalPulses    = 0;
float    totalMeters    = 0;
float    totalCalories  = 0;
uint32_t strokeCount    = 0;
float    currentSpeed   = 0;
float    strokeRate     = 0;
unsigned long workoutStartMs   = 0;
unsigned long workoutElapsedMs = 0;
unsigned long lastStrokeBoundary = 0;   // time of last drive (pull) start, for SPM
bool     inDrive = false;               // true during drive phase (flywheel accelerating)
float    emaIntervalMs   = 0;           // smoothed pulse interval (ms); omega ~ 1/interval
float    extremaInterval = 0;           // turning-point interval for stroke detection
float    emaSpeed        = 0;           // smoothed boat speed (m/s)
// SPM averaged over a trailing window of >= SPM_WINDOW_MS (tune here)
#define  SPM_WINDOW_MS   10000
#define  STROKE_TS_MAX   32
unsigned long strokeTimes[STROKE_TS_MAX] = {};
int      strokeTsHead  = 0;             // ring: next write index
int      strokeTsCount = 0;             // ring: valid entries
// Drag k_eff from recovery deceleration (observational only; NOT fed to distance)
#define  FLYWHEEL_INERTIA 0.1f          // kg*m^2 (rough; only scales displayed k_eff)
float    kEff = 0;                      // smoothed effective drag (kg*m^2)
unsigned long recoveryStartMs = 0;
float    recoveryStartInterval = 0;

// History
#define MAX_HISTORY 10
struct WorkoutRecord {
    char     date[24];
    uint32_t durationSec;
    float    distance;
    uint32_t strokes;
    float    calories;
};
WorkoutRecord history[MAX_HISTORY];
int historyCount = 0;

// ═══════════════════════════════════════════════════════════════════════
//  Capacitive touch — CST328 (V1 boards) or CST3530 (V2 boards)
// ═══════════════════════════════════════════════════════════════════════
// Which controller is fitted depends on the board revision, and the two speak
// genuinely different protocols: CST328 uses 16-bit register addresses, a STOP
// before each read and a 0xCACA handshake; CST3530 uses 32-bit addresses, a
// repeated START, and no handshake at all. Both addresses are probed at boot
// and the matching path is selected, so one binary serves either board.

// CST328 registers (16-bit addressing)
#define CST328_REG_POINT_COUNT   0xD005
#define CST328_REG_XY            0xD000
#define CST328_REG_DEBUG_INFO    0xD101
#define CST328_REG_NORMAL_MODE   0xD109
#define CST328_REG_INFO_TP_NTX   0xD1F4

// CST3530 registers (32-bit addressing)
#define CST3530_REG_DATA         0xD0070000
#define CST3530_REG_COORD_NEXT   0xD0070900
#define CST3530_REG_END_READ     0xD00002AB

enum TouchChip { TOUCH_CHIP_NONE, TOUCH_CHIP_CST328, TOUCH_CHIP_CST3530 };
static TouchChip touchChip = TOUCH_CHIP_NONE;

static bool i2cProbe(uint8_t addr) {
    Wire1.beginTransmission(addr);
    return Wire1.endTransmission(true) == 0;
}

// ── CST328: 16-bit register address, STOP before the read ──
static bool cst328Read(uint16_t reg, uint8_t* buf, size_t len) {
    Wire1.beginTransmission(TOUCH_ADDR_CST328);
    Wire1.write((uint8_t)(reg >> 8));
    Wire1.write((uint8_t)reg);
    if (Wire1.endTransmission(true) != 0) return false;
    if (Wire1.requestFrom((uint8_t)TOUCH_ADDR_CST328, (uint8_t)len) != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire1.read();
    return true;
}

static bool cst328Write(uint16_t reg, const uint8_t* data, size_t len) {
    Wire1.beginTransmission(TOUCH_ADDR_CST328);
    Wire1.write((uint8_t)(reg >> 8));
    Wire1.write((uint8_t)reg);
    for (size_t i = 0; i < len; i++) Wire1.write(data[i]);
    return Wire1.endTransmission(true) == 0;
}

// ── CST3530: 32-bit register address, repeated START before the read ──
static bool cst3530Read(uint32_t reg, uint8_t* buf, size_t len) {
    Wire1.beginTransmission(TOUCH_ADDR_CST3530);
    for (int i = 3; i >= 0; i--) Wire1.write((uint8_t)(reg >> (i * 8)));
    if (Wire1.endTransmission(false) != 0) return false;   // repeated START
    if (Wire1.requestFrom((uint8_t)TOUCH_ADDR_CST3530, (uint8_t)len) != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire1.read();
    return true;
}

static bool cst3530Write(uint32_t reg, const uint8_t* data, size_t len) {
    Wire1.beginTransmission(TOUCH_ADDR_CST3530);
    for (int i = 3; i >= 0; i--) Wire1.write((uint8_t)(reg >> (i * 8)));
    for (size_t i = 0; i < len; i++) Wire1.write(data[i]);
    return Wire1.endTransmission(true) == 0;
}

// Long enough for either part: CST328 needs >100 us low and ~50 ms to return,
// CST3530 asks for 100 ms low and 500 ms to settle.
static void touchReset() {
    digitalWrite(TOUCH_RST, LOW);  delay(100);
    digitalWrite(TOUCH_RST, HIGH); delay(500);
}

static bool touchInit() {
    Wire1.begin(TOUCH_SDA, TOUCH_SCL, TOUCH_I2C_HZ);
    pinMode(TOUCH_INT, INPUT);
    pinMode(TOUCH_RST, OUTPUT);
    touchReset();

    // Scanning first separates "nothing on this bus" (wiring/pins) from "a chip
    // answers but not the one we expected" (board revision) — different fixes.
    Serial.print("[TOUCH] I2C scan:");
    int found = 0;
    for (uint8_t a = 1; a < 127; a++)
        if (i2cProbe(a)) { Serial.printf(" 0x%02X", a); found++; }
    Serial.println(found ? "" : " nothing responded");

    if (i2cProbe(TOUCH_ADDR_CST3530)) {
        touchChip = TOUCH_CHIP_CST3530;
        Serial.println("[TOUCH] CST3530 (V2 board) ready at 0x58");
        return true;                       // no handshake defined for this part
    }

    if (i2cProbe(TOUCH_ADDR_CST328)) {
        uint8_t buf[24] = {};
        cst328Write(CST328_REG_DEBUG_INFO, nullptr, 0);
        if (!cst328Read(CST328_REG_INFO_TP_NTX, buf, sizeof(buf))) {
            Serial.println("[TOUCH] CST328 answered but its info block read failed");
            return false;
        }
        uint16_t verify = ((uint16_t)buf[11] << 8) | buf[10];
        cst328Write(CST328_REG_NORMAL_MODE, nullptr, 0);
        Serial.printf("[TOUCH] CST328 (V1 board) id 0x%04X %s\n", verify,
                      verify == 0xCACA ? "OK" : "UNEXPECTED");
        if (verify != 0xCACA) return false;
        touchChip = TOUCH_CHIP_CST328;
        return true;
    }

    Serial.println("[TOUCH] no known touch controller on the bus");
    return false;
}

// One point only — this UI never needs multitouch.
static bool touchReadPoint(int16_t* px, int16_t* py) {
    int16_t x = 0, y = 0;

    if (touchChip == TOUCH_CHIP_CST3530) {
        uint8_t b[9];
        if (!cst3530Read(CST3530_REG_DATA, b, sizeof(b))) return false;
        uint8_t n = b[3] & 0x0F;
        bool valid = (n >= 1 && n <= 5) && (b[8] & 0xF0);
        // The frame must be released whether or not it held a touch.
        cst3530Write(CST3530_REG_END_READ, nullptr, 0);
        if (!valid) return false;
        // 12-bit coords: low byte separate, high nibbles packed into b[7].
        x = (int16_t)((((uint16_t)b[7] & 0x0F) << 8) | b[4]);
        y = (int16_t)((((uint16_t)b[7] & 0xF0) << 4) | b[5]);

    } else if (touchChip == TOUCH_CHIP_CST328) {
        uint8_t n = 0, clear = 0, d[5];
        if (!cst328Read(CST328_REG_POINT_COUNT, &n, 1)) return false;
        n &= 0x0F;
        if (n == 0 || n > 5) { cst328Write(CST328_REG_POINT_COUNT, &clear, 1); return false; }
        bool ok = cst328Read(CST328_REG_XY, d, sizeof(d));
        cst328Write(CST328_REG_POINT_COUNT, &clear, 1);
        if (!ok) return false;
        // d[1]=X high 8, d[2]=Y high 8, d[3]=low nibbles (X high half, Y low half)
        x = ((int16_t)d[1] << 4) | (d[3] >> 4);
        y = ((int16_t)d[2] << 4) | (d[3] & 0x0F);

    } else {
        return false;
    }

    if (TOUCH_SWAP_XY)  { int16_t t = x; x = y; y = t; }
    if (TOUCH_MIRROR_X) x = TFT_WIDTH  - 1 - x;
    if (TOUCH_MIRROR_Y) y = TFT_HEIGHT - 1 - y;

    *px = constrain(x, (int16_t)0, (int16_t)(TFT_WIDTH  - 1));
    *py = constrain(y, (int16_t)0, (int16_t)(TFT_HEIGHT - 1));
    return true;
}
// ───────── Touch event state ─────────
struct TouchState {
    bool          down;         // finger currently on the panel
    int16_t       x, y;         // latest position
    int16_t       downX, downY; // where the contact started
    unsigned long downMs;
    bool          tap;          // one-shot: a qualified tap completed
    int16_t       tapX, tapY;
    bool          hold;         // one-shot: contact passed ADMIN_HOLD_MS
    bool          holdFired;    // suppress repeat holds within one contact
    int16_t       swipe;        // one-shot: -1 = swipe up, +1 = swipe down
};
TouchState touch = {};
bool uiDirty  = false;   // consumed input -> full redraw next refresh
bool btnDirty = false;   // press/release only -> repaint buttons in place

// Polls the panel and turns raw contacts into tap / hold / swipe events.
void pollTouch() {
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll < TOUCH_POLL_MS) return;
    lastPoll = millis();

    int16_t x, y;
    bool contact = touchReadPoint(&x, &y);
    unsigned long now = millis();

    if (contact) {
        if (!touch.down) {                       // press edge
            touch.down   = true;
            touch.downX  = x;  touch.downY = y;
            touch.downMs = now;
            touch.holdFired = false;
            btnDirty = true;                     // show the pressed state
        }
        touch.x = x; touch.y = y;
        if (!touch.holdFired && now - touch.downMs >= ADMIN_HOLD_MS) {
            touch.hold = true;
            touch.holdFired = true;
        }
    } else if (touch.down) {                     // release edge
        touch.down = false;
        btnDirty = true;
        int16_t dx = touch.x - touch.downX;
        int16_t dy = touch.y - touch.downY;
        unsigned long held = now - touch.downMs;

        if (touch.holdFired) {
            // already delivered as a hold; a tap would double-fire the action
        } else if (abs(dy) > 60 && abs(dx) < 45) {
            touch.swipe = (dy > 0) ? 1 : -1;
        } else if (held >= TAP_MIN_MS &&
                   abs(dx) <= TAP_MAX_DRIFT && abs(dy) <= TAP_MAX_DRIFT) {
            touch.tap  = true;
            touch.tapX = touch.downX;
            touch.tapY = touch.downY;
        }
    }
}

// ───────── Touch button registry ─────────
// draw*Screen() declares its targets; the same rectangles are hit-tested, so
// the drawn button and the tappable area cannot drift apart.
enum BtnId {
    B_NONE = 0,
    B_START, B_HISTORY, B_BACK, B_OK, B_PAUSE, B_RESUME, B_FINISH,
    B_PREV, B_NEXT, B_SAVE, B_CANCEL, B_EXIT, B_EDIT,
    B_BRIGHT_DN, B_BRIGHT_UP,
    B_DEC_BIG, B_DEC, B_INC, B_INC_BIG,
    B_SCROLL_UP, B_SCROLL_DN,
    B_ROW0, B_ROW1, B_ROW2, B_ROW3, B_ROW4, B_ROW5, B_ROW6, B_ROW7, B_ROW8,
    B_KEY0, B_KEY1, B_KEY2, B_KEY3, B_KEY4,
    B_KEY5, B_KEY6, B_KEY7, B_KEY8, B_KEY9,
    B_KEYDEL, B_KEYESC,
};

struct Btn {
    uint8_t  id;
    int16_t  x, y, w, h;
    const char* label;
    uint16_t color;
    bool     flat;      // list row: no rounded frame, highlight when selected
    bool     selected;
};
#define MAX_BTNS 16
Btn  curBtns[MAX_BTNS];
int  curBtnCount = 0;

void beginBtns() { curBtnCount = 0; }

void addBtn(uint8_t id, int16_t x, int16_t y, int16_t w, int16_t h,
            const char* label, uint16_t color, bool flat = false,
            bool selected = false) {
    if (curBtnCount >= MAX_BTNS) return;
    curBtns[curBtnCount++] = {id, x, y, w, h, label, color, flat, selected};
}

// Bottom action bar: `n` equal buttons across the full width.
void addBarBtn(uint8_t id, int idx, int n, const char* label, uint16_t color) {
    int16_t w = (TFT_WIDTH - UI_MARGIN * 2 - UI_MARGIN * (n - 1)) / n;
    addBtn(id, UI_MARGIN + idx * (w + UI_MARGIN), BTN_Y, w, BTN_H, label, color);
}

void drawBtn(const Btn& b, bool pressed) {
    if (b.flat) {
        uint16_t bg = pressed ? COL_HEADER_BG : (b.selected ? COL_BTN_BG : COL_BG);
        tft.fillRect(b.x, b.y, b.w, b.h, bg);
        if (b.selected) tft.drawRect(b.x, b.y, b.w, b.h, b.color);
        return;   // the caller paints the row contents
    }
    uint16_t bg = pressed ? b.color   : COL_BTN_BG;
    uint16_t fg = pressed ? COL_BG    : b.color;
    tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, bg);
    tft.drawRoundRect(b.x, b.y, b.w, b.h, 6, b.color);
    if (!b.label || !b.label[0]) return;

    uint8_t size = (b.h >= 40) ? 2 : 1;
    int16_t tw = (int16_t)strlen(b.label) * 6 * size;
    if (tw > b.w - 6 && size > 1) { size = 1; tw = (int16_t)strlen(b.label) * 6; }
    tft.setTextSize(size);
    tft.setTextColor(fg, bg);
    tft.setCursor(b.x + (b.w - tw) / 2, b.y + (b.h - 8 * size) / 2);
    tft.print(b.label);
}

bool btnIsPressed(const Btn& b) {
    return touch.down &&
           touch.x >= b.x && touch.x < b.x + b.w &&
           touch.y >= b.y && touch.y < b.y + b.h;
}

void drawAllBtns() {
    for (int i = 0; i < curBtnCount; i++)
        if (!curBtns[i].flat) drawBtn(curBtns[i], btnIsPressed(curBtns[i]));
}

// Consumes the pending tap and reports which button it landed on.
uint8_t tappedBtn() {
    if (!touch.tap) return B_NONE;
    for (int i = 0; i < curBtnCount; i++) {
        const Btn& b = curBtns[i];
        if (touch.tapX >= b.x && touch.tapX < b.x + b.w &&
            touch.tapY >= b.y && touch.tapY < b.y + b.h) {
            touch.tap = false;
            uiDirty = true;
            return b.id;
        }
    }
    touch.tap = false;   // tap landed on dead space
    return B_NONE;
}

// ═══════════════════════════════════════════════════════════════════════
//  Audio — on-board PCM5101 I2S DAC
// ═══════════════════════════════════════════════════════════════════════
static bool audioReady = false;

void initAudio() {
    i2s_config_t cfg = {};
    cfg.mode                = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate         = I2S_SAMPLE_RATE;
    cfg.bits_per_sample     = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format      = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags    = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count       = 4;
    cfg.dma_buf_len         = 256;
    cfg.use_apll            = false;
    cfg.tx_desc_auto_clear  = true;
    cfg.fixed_mclk          = 0;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = I2S_BCLK;
    pins.ws_io_num    = I2S_LRCK;
    pins.data_out_num = I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL) != ESP_OK) return;
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) return;
    i2s_zero_dma_buffer(I2S_NUM_0);
    audioReady = true;
}

// Blocking, like the PWM tone() it replaces — callers rely on that.
// A 4 ms attack/release ramp keeps the amplifier from clicking on each beep.
void playTone(uint16_t freq, uint16_t durationMs) {
    if (!audioReady || freq == 0 || durationMs == 0) return;

    const uint32_t total   = (uint32_t)I2S_SAMPLE_RATE * durationMs / 1000;
    const uint32_t rampMax = (uint32_t)I2S_SAMPLE_RATE * 4 / 1000;   // 4 ms
    const uint32_t ramp    = (rampMax < total / 2) ? rampMax : total / 2;
    const float    step    = 2.0f * PI * freq / I2S_SAMPLE_RATE;
    const float    peak    = 32767.0f * TONE_VOLUME;

    static int16_t frames[128 * 2];
    float phase = 0;
    uint32_t done = 0;
    while (done < total) {
        uint32_t n = (total - done < 128) ? (total - done) : 128;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t pos = done + i;
            float env = 1.0f;
            if (ramp > 0) {
                if (pos < ramp)               env = (float)pos / ramp;
                else if (total - pos < ramp)  env = (float)(total - pos) / ramp;
            }
            int16_t s = (int16_t)(peak * env * sinf(phase));
            phase += step;
            if (phase > 2.0f * PI) phase -= 2.0f * PI;
            frames[i * 2] = s; frames[i * 2 + 1] = s;
        }
        size_t written = 0;
        i2s_write(I2S_NUM_0, frames, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        done += n;
    }
    i2s_zero_dma_buffer(I2S_NUM_0);
}

void beep() {
    playTone(TONE_BEEP, 50);
}

// ───────── Backlight ─────────
void setBacklight(uint8_t val) {
    ledcWrite(0, val);
}

// ───────── Sensor Reading ─────────

#ifdef SENSOR_TYPE_LDR
// LDR is sampled by a 1 kHz hardware-timer ISR so pulses are never lost while the
// loop is busy (the periodic full-screen redraw blocks the loop for ~10-20 ms).
// The ISR only touches the ADC while a workout is active — the one window in which
// no flash/NVS writes happen — so analogRead() from the ISR is safe in this firmware.
void IRAM_ATTR ldrSampleISR() {
    if (!workoutActive) return;
    int val = analogRead(LDR_SIGNAL_PIN);
    if (!ldrPulseActive && val < (LDR_THRESHOLD - LDR_HYSTERESIS)) {
        ldrPulseActive = true;                       // falling edge: paddle passing
        unsigned long now = micros();
        if (ldrLastPulseUs > 0) ldrPulseIntervalUs = now - ldrLastPulseUs;
        ldrLastPulseUs = now;
        ldrPulseCount++;
    } else if (ldrPulseActive && val > (LDR_THRESHOLD + LDR_HYSTERESIS)) {
        ldrPulseActive = false;                      // rising edge: light restored
    }
}

void readSensor() {
    if (!workoutActive) return;

    noInterrupts();
    unsigned long pc = ldrPulseCount;
    unsigned long lp = ldrLastPulseUs;
    interrupts();

    if (pc > pulseCount) {
        lastPulseMs = millis();
        lastPulseUsSnap = lp;
        pulseCount = pc;
    }
}

const char* getSensorLabel() { return "LDR"; }
void getSensorText(char* buf, size_t n) {
    snprintf(buf, n, "LDR %-4d (thr %d)", analogRead(LDR_SIGNAL_PIN), LDR_THRESHOLD);
}
#endif

#ifdef SENSOR_TYPE_HALL
void IRAM_ATTR hallISR() {
    unsigned long now = micros();
    unsigned long interval = now - hallLastPulseUs;
    if (interval > HALL_MIN_PULSE_US) {
        hallPulseIntervalUs = interval;
        hallLastPulseUs = now;
        hallPulseCount++;
    }
}

void readSensor() {
    if (!workoutActive) return;

    noInterrupts();
    unsigned long pc = hallPulseCount;
    unsigned long lp = hallLastPulseUs;
    interrupts();

    if (pc > pulseCount) {
        lastPulseMs = millis();
        lastPulseUsSnap = lp;
        pulseCount = pc;
    }
}

const char* getSensorLabel() { return "HALL"; }
void getSensorText(char* buf, size_t n) {
    snprintf(buf, n, "HALL: %-7s", digitalRead(HALL_SENSOR_PIN) == LOW ? "MAGNET" : "clear");
}
#endif

#ifdef SENSOR_TYPE_BLOCKER
void IRAM_ATTR blockerISR() {
    unsigned long now = micros();
    unsigned long interval = now - blockerLastPulseUs;
    if (interval > BLOCKER_MIN_PULSE_US) {
        blockerPulseIntervalUs = interval;
        blockerLastPulseUs = now;
        blockerPulseCount++;
    }
}

void readSensor() {
    if (!workoutActive) return;

    noInterrupts();
    unsigned long pc = blockerPulseCount;
    unsigned long lp = blockerLastPulseUs;
    interrupts();

    if (pc > pulseCount) {
        lastPulseMs = millis();
        lastPulseUsSnap = lp;
        pulseCount = pc;
    }
}

const char* getSensorLabel() { return "BLKR"; }
void getSensorText(char* buf, size_t n) {
    bool blocked = (digitalRead(BLOCKER_SENSOR_PIN) == LOW) == BLOCKER_ACTIVE_LOW;
    snprintf(buf, n, "SENSOR: %-8s", blocked ? "BLOCKED" : "clear");
}
#endif

// ───────── BLE FTMS Setup & Update ─────────
void initBLE() {
    BLEDevice::init("WaterRower-" MACHINE_SN);
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new FTMSCallbacks());

    // FTMS Service
    BLEService* ftmsService = bleServer->createService(FTMS_SERVICE_UUID);

    // FTMS Feature (required) — declare rower support
    ftmsFeatureChar = ftmsService->createCharacteristic(
        FTMS_FEATURE_UUID, BLECharacteristic::PROPERTY_READ);
    // Fitness Machine Features: Rower supported
    // Byte 0-3: Machine Features, Byte 4-7: Target Setting Features
    uint8_t ftmsFeatures[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ftmsFeatures[0] = 0x26; // stroke rate, total distance, pace, calories
    ftmsFeatureChar->setValue(ftmsFeatures, 8);

    // Rower Data (notify)
    rowerDataChar = ftmsService->createCharacteristic(
        ROWER_DATA_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    rowerDataChar->addDescriptor(new BLE2902());

    ftmsService->start();

    // Advertise
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(FTMS_SERVICE_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
}

void updateBLE() {
    if (!bleClientConnected) return;
    if (!workoutActive) return;
    if (millis() - lastBleUpdate < BLE_UPDATE_INTERVAL_MS) return;
    lastBleUpdate = millis();

    // FTMS Rower Data format (per Bluetooth spec)
    // Flags (2 bytes) + fields
    unsigned long elapsed = workoutElapsedMs + (millis() - workoutStartMs);
    uint16_t strokeRateX2 = (uint16_t)(strokeRate * 2);  // 0.5 resolution
    uint32_t totalDist = (uint32_t)totalMeters;           // FTMS total distance is uint24 (m)
    uint16_t paceS500 = 0;
    if (currentSpeed > 0.1f) {
        paceS500 = (uint16_t)(500.0f / currentSpeed);    // seconds per 500m
    }
    uint16_t totalCal = (uint16_t)totalCalories;
    uint16_t elapsedSec = (uint16_t)(elapsed / 1000);

    // Flags: bits indicate which fields are present
    // Bit 1: stroke rate + stroke count
    // Bit 2: total distance
    // Bit 4: pace
    // Bit 9: total energy
    // Bit 11: elapsed time
    uint16_t flags = 0;
    flags |= (1 << 1);  // stroke rate present
    flags |= (1 << 2);  // total distance present
    flags |= (1 << 4);  // instantaneous pace present
    flags |= (1 << 9);  // energy present
    flags |= (1 << 11); // elapsed time present

    uint8_t data[20];
    int idx = 0;
    data[idx++] = flags & 0xFF;
    data[idx++] = (flags >> 8) & 0xFF;
    // Stroke rate (uint16, 0.5 resolution)
    data[idx++] = strokeRateX2 & 0xFF;
    data[idx++] = (strokeRateX2 >> 8) & 0xFF;
    // Stroke count (uint16)
    data[idx++] = strokeCount & 0xFF;
    data[idx++] = (strokeCount >> 8) & 0xFF;
    // Total distance (uint24, little endian)
    data[idx++] = totalDist & 0xFF;
    data[idx++] = (totalDist >> 8) & 0xFF;
    data[idx++] = (totalDist >> 16) & 0xFF;
    // Instantaneous pace (uint16, seconds per 500m)
    data[idx++] = paceS500 & 0xFF;
    data[idx++] = (paceS500 >> 8) & 0xFF;
    // Energy: total (uint16) + per hour (uint16) + per min (uint8)
    data[idx++] = totalCal & 0xFF;
    data[idx++] = (totalCal >> 8) & 0xFF;
    data[idx++] = 0; data[idx++] = 0; // cal/hour (not calculated)
    data[idx++] = 0;                   // cal/min
    // Elapsed time (uint16, seconds)
    data[idx++] = elapsedSec & 0xFF;
    data[idx++] = (elapsedSec >> 8) & 0xFF;

    rowerDataChar->setValue(data, idx);
    rowerDataChar->notify();
}

// ───────── WiFi (multi-network, auto-switch) ─────────
static const char* wifiSSIDs[]     = WIFI_SSIDS;
static const char* wifiPasswords[] = WIFI_PASSWORDS;

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);

    // Scan and find known networks sorted by RSSI
    int found = WiFi.scanNetworks(false, true);
    int bestIdx = -1;
    int bestRSSI = -999;

    for (int s = 0; s < found; s++) {
        for (int k = 0; k < WIFI_COUNT; k++) {
            if (WiFi.SSID(s) == wifiSSIDs[k]) {
                if (WiFi.RSSI(s) > bestRSSI) {
                    bestRSSI = WiFi.RSSI(s);
                    bestIdx  = k;
                }
            }
        }
    }
    WiFi.scanDelete();

    if (bestIdx < 0) return;  // none found

    WiFi.begin(wifiSSIDs[bestIdx], wifiPasswords[bestIdx]);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 40) {
        delay(500);
        tries++;
    }
}

// ───────── Timestamp ─────────
String getTimestamp() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        return String(buf);
    }
    return "1970-01-01T00:00:00Z";
}

// ───────── TrueNAS Upload (plain HTTP, local network) ─────────
bool uploadToNAS(uint32_t durSec) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return false;

    StaticJsonDocument<1024> doc;
    doc["machine_sn"]      = MACHINE_SN;
    doc["machine_model"]   = MACHINE_MODEL;
    doc["date"]            = getTimestamp();
    doc["user"]            = (currentUser < USER_COUNT) ? userNames[currentUser] : "Guest";
    doc["duration_sec"]    = durSec;
    doc["distance_m"]      = totalMeters;
    doc["strokes"]         = strokeCount;
    doc["calories"]        = totalCalories;
    doc["avg_speed_ms"]    = (durSec > 0) ? totalMeters / durSec : 0;
    doc["avg_stroke_rate"] = (durSec > 0) ? strokeCount * 60.0f / durSec : 0;
    if (totalMeters > 0) {
        float secPer500 = (durSec * 500.0f) / totalMeters;
        char split[16];
        snprintf(split, sizeof(split), "%d:%02d", (int)(secPer500/60), (int)secPer500%60);
        doc["split_500m"] = split;
    }
    String content;
    serializeJsonPretty(doc, content);

    char filepath[128];
    struct tm ti;
    const char* uname = (currentUser < USER_COUNT) ? userNames[currentUser] : "Guest";
    if (getLocalTime(&ti, 200)) {
        snprintf(filepath, sizeof(filepath), "%s/%s_%04d%02d%02d_%02d%02d%02d.json",
            NAS_PATH, uname,
            ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday,
            ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        snprintf(filepath, sizeof(filepath), "%s/%s_%lu.json", NAS_PATH, uname, millis());
    }

    String boundary = "WR32";
    String meta = String("{\"path\":\"") + filepath + "\"}";
    String body;
    body.reserve(content.length() + 256);
    body  = "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"data\"\r\n\r\n";
    body += meta + "\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"workout.json\"\r\n";
    body += "Content-Type: application/octet-stream\r\n\r\n";
    body += content + "\r\n";
    body += "--" + boundary + "--\r\n";

    WiFiClient wc;
    HTTPClient http;
    http.setTimeout(10000);
    http.begin(wc, "http://" NAS_HOST "/api/v2.0/filesystem/put");
    http.addHeader("Authorization", "Bearer " NAS_API_KEY);
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

    Serial.printf("[NAS] -> %s\n", filepath);
    int code = http.POST(body);
    Serial.printf("[NAS] HTTP %d\n", code);
    http.end();
    return (code == 200);
}

// ───────── Drawing Helpers ─────────

// Adafruit_GFX has no scaled bitmap blit; the 16x16 icons would be ~3 mm on a
// 2.8" panel, so set pixels are expanded into scale x scale blocks.
void drawIcon(int16_t x, int16_t y, const uint8_t* bmp, uint16_t color, uint8_t scale) {
    for (int16_t j = 0; j < 16; j++) {
        uint16_t row = (pgm_read_byte(&bmp[j * 2]) << 8) | pgm_read_byte(&bmp[j * 2 + 1]);
        int16_t runStart = -1;
        for (int16_t i = 0; i <= 16; i++) {
            bool on = (i < 16) && (row & (0x8000 >> i));
            if (on && runStart < 0) runStart = i;
            if (!on && runStart >= 0) {           // flush the horizontal run
                tft.fillRect(x + runStart * scale, y + j * scale,
                             (i - runStart) * scale, scale, color);
                runStart = -1;
            }
        }
    }
}

void drawHeader(const char* title) {
    tft.fillRect(0, 0, TFT_WIDTH, HEADER_H, COL_HEADER_BG);
    tft.setTextSize(2);
    tft.setTextColor(COL_ACCENT, COL_HEADER_BG);
    tft.setCursor(8, (HEADER_H - 16) / 2);
    tft.print(title);
    // BLE indicator
    tft.setTextSize(1);
    tft.setCursor(TFT_WIDTH - 52, (HEADER_H - 8) / 2);
    tft.setTextColor(bleClientConnected ? COL_VALUE : COL_LABEL, COL_HEADER_BG);
    tft.print(bleClientConnected ? "BT" : "bt");
    // WiFi indicator
    tft.setCursor(TFT_WIDTH - 28, (HEADER_H - 8) / 2);
    tft.setTextColor(WiFi.status() == WL_CONNECTED ? COL_VALUE : COL_ERROR, COL_HEADER_BG);
    tft.print(WiFi.status() == WL_CONNECTED ? "W+" : "W-");
}

void drawDivider(int y) {
    tft.drawFastHLine(UI_MARGIN, y, TFT_WIDTH - UI_MARGIN * 2, COL_DIVIDER);
}

// Small caps label with a big value underneath.
void drawLabelValue(int x, int y, const char* label, const char* value,
                    uint16_t valColor = COL_VALUE, uint8_t valSize = 3) {
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(x, y);
    tft.print(label);
    tft.setTextColor(valColor, COL_BG);
    tft.setCursor(x, y + 14);
    tft.setTextSize(valSize);
    tft.print(value);
}

void drawCentered(int16_t y, const char* text, uint8_t size, uint16_t color) {
    int16_t w = (int16_t)strlen(text) * 6 * size;
    tft.setTextSize(size);
    tft.setTextColor(color, COL_BG);
    tft.setCursor((TFT_WIDTH - w) / 2, y);
    tft.print(text);
}

// Word-wrapped single-line-height text, clipped to `lines` rows.
void drawWrapped(int16_t x, int16_t y, int16_t w, const char* text, uint16_t color, int lines) {
    tft.setTextSize(1);
    tft.setTextColor(color, COL_BG);
    int maxChars = w / 6;
    int len = strlen(text), pos = 0, row = 0;
    while (pos < len && row < lines) {
        int take = min(maxChars, len - pos);
        if (pos + take < len) {                   // break on the last space
            int brk = take;
            while (brk > 0 && text[pos + brk] != ' ') brk--;
            if (brk > 0) take = brk;
        }
        tft.setCursor(x, y + row * 10);
        for (int i = 0; i < take; i++) tft.print(text[pos + i]);
        pos += take;
        while (pos < len && text[pos] == ' ') pos++;
        row++;
    }
}

// ───────── Screens ─────────

void drawIdleScreen() {
    char buf[40];

    drawHeader("WATER ROWER");

    drawIcon(UI_MARGIN, CONTENT_Y + 4, bmp_rower, COL_ACCENT, 2);
    for (int i = 0; i < 5; i++)
        drawIcon(48 + i * 34, CONTENT_Y + 12, bmp_wave, BLUE, 2);

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 42);
    tft.printf("%s  SN %s", MACHINE_MODEL, MACHINE_SN);

    drawDivider(CONTENT_Y + 56);

    getSensorText(buf, sizeof(buf));
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 64);
    tft.print(buf);

    drawCentered(CONTENT_Y + 80, "READY", 4, COL_ACCENT);

    // Rotating idle message (lengths differ: clear the block first)
    tft.fillRect(0, CONTENT_Y + 120, TFT_WIDTH, 22, COL_BG);
    drawWrapped(UI_MARGIN, CONTENT_Y + 120, TFT_WIDTH - UI_MARGIN * 2,
                idle_msgs[millis() / 3000 % NUM_IDLE_MSGS], COL_WARN, 2);

    // Brightness row
    int16_t by = CONTENT_Y + 150;
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(UI_MARGIN, by);
    tft.print("BRIGHTNESS");

    int16_t bary = by + 14, barx = UI_MARGIN + 52, barw = TFT_WIDTH - barx - 52 - UI_MARGIN;
    tft.drawRect(barx, bary + 8, barw, 12, COL_DIVIDER);
    tft.fillRect(barx + 1, bary + 9, (barw - 2) * brightness / 255, 10, COL_ACCENT);
    tft.fillRect(barx + 1 + (barw - 2) * brightness / 255, bary + 9,
                 (barw - 2) - (barw - 2) * brightness / 255, 10, COL_BG);

    beginBtns();
    addBtn(B_BRIGHT_DN, UI_MARGIN, bary, 44, 28, "-", COL_LABEL);
    addBtn(B_BRIGHT_UP, TFT_WIDTH - UI_MARGIN - 44, bary, 44, 28, "+", COL_LABEL);
    addBarBtn(B_HISTORY, 0, 2, "HISTORY", COL_LABEL);
    addBarBtn(B_START,   1, 2, "START",   COL_VALUE);
    drawAllBtns();

    tft.setTextSize(1);
    tft.setTextColor(COL_DIVIDER, COL_BG);
    tft.setCursor(UI_MARGIN, BAR_Y - 14);
    tft.print("Hold the title bar 3s for admin");
}

void drawWorkoutScreen() {
    char buf[24];

    drawHeader("ROWING");

    unsigned long elapsed = workoutElapsedMs + (millis() - workoutStartMs);
    uint32_t sec = elapsed / 1000;
    uint32_t mn = sec / 60;
    uint32_t s  = sec % 60;

    // Animated rower icon (alternates position)
    tft.fillRect(UI_MARGIN, CONTENT_Y + 2, 44, 32, COL_BG);
    int rowerX = UI_MARGIN + (millis() / 400 % 3) * 4;
    drawIcon(rowerX, CONTENT_Y + 2, bmp_rower, COL_ACCENT, 2);

    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)mn, (unsigned long)s);
    tft.setTextSize(4);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setCursor(72, CONTENT_Y + 2);
    tft.print(buf);

    drawDivider(CONTENT_Y + 36);

    snprintf(buf, sizeof(buf), "%7.1f", totalMeters);
    drawLabelValue(UI_MARGIN, CONTENT_Y + 42, "DISTANCE (m)", buf, COL_VALUE, 4);

    // Fire icon when moving fast
    tft.fillRect(TFT_WIDTH - 40, CONTENT_Y + 56, 32, 32, COL_BG);
    if (currentSpeed > 2.5f)
        drawIcon(TFT_WIDTH - 40, CONTENT_Y + 56, bmp_fire, ST77XX_RED, 2);

    if (currentSpeed > 0.1f) {
        float secPer500 = 500.0f / currentSpeed;
        snprintf(buf, sizeof(buf), "%2d:%02d", (int)(secPer500 / 60), (int)secPer500 % 60);
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }
    drawLabelValue(UI_MARGIN, CONTENT_Y + 94, "/500m SPLIT", buf, COL_ACCENT, 4);

    drawDivider(CONTENT_Y + 146);

    // SPM | STROKES | CAL
    const int16_t colX[3] = {UI_MARGIN, 88, 168};
    const char* colLbl[3] = {"SPM", "STROKES", "CAL"};
    tft.setTextSize(1);
    for (int i = 0; i < 3; i++) {
        tft.setTextColor(COL_LABEL, COL_BG);
        tft.setCursor(colX[i], CONTENT_Y + 152);
        tft.print(colLbl[i]);
    }
    tft.setTextSize(3);
    tft.setTextColor(COL_VALUE, COL_BG);
    tft.setCursor(colX[0], CONTENT_Y + 164); tft.printf("%-3.0f", strokeRate);
    tft.setCursor(colX[1], CONTENT_Y + 164); tft.printf("%-4lu", (unsigned long)strokeCount);
    tft.setCursor(colX[2], CONTENT_Y + 164); tft.printf("%-3.0f", totalCalories);

    // Motivational message (changes every 10 seconds)
    tft.fillRect(0, BAR_Y - 32, TFT_WIDTH, 10, COL_BG);
    tft.setTextSize(1);
    tft.setTextColor(COL_WARN, COL_BG);
    tft.setCursor(UI_MARGIN, BAR_Y - 32);
    tft.print(fun_msgs[(sec / 10) % NUM_FUN_MSGS]);

    // Animated water above the action bar
    tft.fillRect(0, BAR_Y - 18, TFT_WIDTH, 16, COL_BG);
    int waveOffset = (millis() / 300) % 16;
    for (int x = waveOffset - 16; x < TFT_WIDTH; x += 16)
        drawIcon(x, BAR_Y - 18, bmp_wave, BLUE, 1);

    beginBtns();
    addBarBtn(B_PAUSE, 0, 1, "PAUSE", COL_WARN);
    drawAllBtns();
}

void drawPausedScreen() {
    char buf[24];

    drawHeader("PAUSED");

    uint32_t sec = workoutElapsedMs / 1000;

    drawIcon(UI_MARGIN, CONTENT_Y + 4, bmp_skull, COL_WARN, 2);

    tft.setTextSize(3);
    tft.setTextColor(COL_WARN, COL_BG);
    tft.setCursor(52, CONTENT_Y + 4);
    tft.print("PAUSED");

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(52, CONTENT_Y + 30);
    tft.print("Tired already?");

    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    tft.setTextSize(4);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 50);
    tft.print(buf);

    drawDivider(CONTENT_Y + 92);

    tft.setTextSize(2);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 102);  tft.printf("Dist %.1f m ", totalMeters);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 124);  tft.printf("Strokes %lu ", (unsigned long)strokeCount);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 146);  tft.printf("Cal %.0f ", totalCalories);
    tft.setTextSize(1);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 172);  tft.printf("Drag k: %.4f", kEff);

    tft.setTextColor(COL_ACCENT, COL_BG);
    tft.setCursor(UI_MARGIN, BAR_Y - 16);
    tft.print("RESUME = get back in there!");

    beginBtns();
    addBarBtn(B_FINISH, 0, 2, "FINISH", COL_ERROR);
    addBarBtn(B_RESUME, 1, 2, "RESUME", COL_VALUE);
    drawAllBtns();
}

void drawSummaryScreen(bool uploadOk) {
    char buf[32];

    tft.fillScreen(COL_BG);
    drawHeader("SUMMARY");

    drawIcon(UI_MARGIN, CONTENT_Y + 4, bmp_trophy, COL_WARN, 2);

    uint32_t sec = workoutElapsedMs / 1000;

    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    drawLabelValue(52, CONTENT_Y + 4, "TIME", buf, COL_TEXT, 4);

    snprintf(buf, sizeof(buf), "%.1f", totalMeters);
    drawLabelValue(UI_MARGIN, CONTENT_Y + 56, "DISTANCE (m)", buf, COL_VALUE, 4);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)strokeCount);
    drawLabelValue(UI_MARGIN, CONTENT_Y + 112, "STROKES", buf, COL_ACCENT, 3);

    snprintf(buf, sizeof(buf), "%.0f", totalCalories);
    drawLabelValue(130, CONTENT_Y + 112, "CALORIES", buf, COL_WARN, 3);

    drawDivider(CONTENT_Y + 156);

    tft.setTextSize(1);
    tft.setTextColor(COL_WARN, COL_BG);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 164);
    tft.print(done_msgs[sec % NUM_DONE_MSGS]);

    // Upload status
    int16_t sy = CONTENT_Y + 182;
    if (currentUser == USER_COUNT) {
        drawIcon(UI_MARGIN, sy, bmp_happy, COL_WARN, 2);
        tft.setTextColor(COL_WARN, COL_BG);
    } else if (uploadOk) {
        drawIcon(UI_MARGIN, sy, bmp_happy, COL_VALUE, 2);
        tft.setTextColor(COL_VALUE, COL_BG);
    } else {
        drawIcon(UI_MARGIN, sy, bmp_sad, COL_ERROR, 2);
        tft.setTextColor(COL_ERROR, COL_BG);
    }
    tft.setTextSize(2);
    tft.setCursor(48, sy + 8);
    tft.print(currentUser == USER_COUNT ? "Guest: BT only"
              : uploadOk ? "Saved to NAS!" : "Upload FAILED");

    beginBtns();
    addBarBtn(B_OK, 0, 1, "DONE", COL_ACCENT);
    drawAllBtns();
}

void drawHistoryScreen() {
    char buf[32];

    drawHeader("HISTORY");

    beginBtns();
    if (historyCount == 0) {
        drawCentered(CONTENT_Y + 80, "No records yet", 2, COL_LABEL);
    } else {
        int idx = displayPage;
        if (idx >= historyCount) idx = historyCount - 1;
        WorkoutRecord &r = history[idx];

        tft.setTextSize(2);
        tft.setTextColor(COL_ACCENT, COL_BG);
        tft.setCursor(UI_MARGIN, CONTENT_Y + 4);
        tft.printf("Record %d/%d ", idx + 1, historyCount);

        tft.setTextSize(1);
        tft.setTextColor(COL_LABEL, COL_BG);
        tft.setCursor(UI_MARGIN, CONTENT_Y + 26);
        tft.print(r.date);

        drawDivider(CONTENT_Y + 40);

        snprintf(buf, sizeof(buf), "%02lu:%02lu",
                 (unsigned long)(r.durationSec / 60), (unsigned long)(r.durationSec % 60));
        drawLabelValue(UI_MARGIN, CONTENT_Y + 48, "TIME", buf, COL_TEXT, 4);

        snprintf(buf, sizeof(buf), "%.0f", r.distance);
        drawLabelValue(UI_MARGIN, CONTENT_Y + 104, "DISTANCE (m)", buf, COL_VALUE, 4);

        snprintf(buf, sizeof(buf), "%lu", (unsigned long)r.strokes);
        drawLabelValue(UI_MARGIN, CONTENT_Y + 160, "STROKES", buf, COL_ACCENT, 2);

        snprintf(buf, sizeof(buf), "%.0f", r.calories);
        drawLabelValue(130, CONTENT_Y + 160, "CALORIES", buf, COL_WARN, 2);

        addBarBtn(B_PREV, 0, 3, "PREV", displayPage > 0 ? COL_ACCENT : COL_DIVIDER);
        addBarBtn(B_BACK, 1, 3, "BACK", COL_LABEL);
        addBarBtn(B_NEXT, 2, 3, "NEXT",
                  displayPage < historyCount - 1 ? COL_ACCENT : COL_DIVIDER);
        drawAllBtns();
        return;
    }
    addBarBtn(B_BACK, 0, 1, "BACK", COL_LABEL);
    drawAllBtns();
}

void drawUploadingScreen() {
    tft.fillScreen(COL_BG);   // called directly from loop(), must clear itself
    drawHeader("SAVING...");

    drawIcon((TFT_WIDTH - 32) / 2, CONTENT_Y + 30, bmp_happy, COL_WARN, 2);

    drawCentered(CONTENT_Y + 80, "Uploading", 3, COL_WARN);
    drawCentered(CONTENT_Y + 120, "Sending to NAS...", 1, COL_LABEL);
    drawCentered(CONTENT_Y + 136, "Don't pull the plug!", 1, COL_LABEL);

    for (int x = 0; x < TFT_WIDTH; x += 32)
        drawIcon(x, TFT_HEIGHT - 48, bmp_wave, BLUE, 2);

    beginBtns();   // no touch targets while uploading
}

// Shared scrolling list: `total` rows, `sel` highlighted, drawn from `scroll`.
// The row body is painted by the caller through a callback-free convention:
// this only draws the selection frame and registers B_ROW0.. hit targets.
#define LIST_ROW_H  42
int listVisibleRows() { return (BAR_Y - CONTENT_Y - 4) / LIST_ROW_H; }

int16_t listRowY(int row) { return CONTENT_Y + 4 + row * LIST_ROW_H; }

void drawUserSelectScreen() {
    drawHeader("WHO ARE YOU?");

    int total = USER_COUNT + 1;                 // +1 for Guest
    int vis   = min(listVisibleRows(), total);
    if (currentUser < userScrollOffset) userScrollOffset = currentUser;
    if (currentUser >= userScrollOffset + vis) userScrollOffset = currentUser - vis + 1;

    beginBtns();
    for (int row = 0; row < vis; row++) {
        int i = userScrollOffset + row;
        if (i >= total) break;
        bool isGuest = (i == USER_COUNT);
        const char* name = isGuest ? "Guest" : userNames[i];
        int16_t y = listRowY(row);
        bool sel = (i == currentUser);

        uint16_t rowBg = sel ? COL_BTN_BG : COL_BG;
        tft.fillRect(UI_MARGIN, y, TFT_WIDTH - UI_MARGIN * 2, LIST_ROW_H - 4, rowBg);
        if (sel) tft.drawRect(UI_MARGIN, y, TFT_WIDTH - UI_MARGIN * 2, LIST_ROW_H - 4, COL_ACCENT);

        tft.setTextSize(2);
        tft.setTextColor(isGuest ? COL_WARN : (sel ? COL_ACCENT : COL_LABEL), rowBg);
        tft.setCursor(UI_MARGIN + 12, y + (LIST_ROW_H - 4 - 16) / 2);
        tft.print(name);

        if (!isGuest) {
            tft.setTextSize(1);
            tft.setTextColor(COL_DIVIDER, rowBg);
            tft.setCursor(TFT_WIDTH - UI_MARGIN - 34, y + (LIST_ROW_H - 4 - 8) / 2);
            tft.printf("%d/%d", i + 1, USER_COUNT);
        }
        addBtn(B_ROW0 + row, UI_MARGIN, y, TFT_WIDTH - UI_MARGIN * 2, LIST_ROW_H - 4,
               nullptr, COL_ACCENT, true, sel);
    }

    if (total > vis) {
        tft.setTextSize(1);
        tft.setTextColor(COL_LABEL, COL_BG);
        tft.setCursor(TFT_WIDTH / 2 - 30, BAR_Y - 12);
        tft.print("swipe to scroll");
    }

    addBarBtn(B_BACK,  0, 2, "BACK",  COL_LABEL);
    addBarBtn(B_START, 1, 2, "START", COL_VALUE);
    drawAllBtns();
}

void drawAdminMenuScreen() {
    drawHeader("ADMIN MENU");

    static const char* const items[ADMIN_MENU_COUNT] =
        {"Calibration", "User Weights", "Password"};

    beginBtns();
    for (int i = 0; i < ADMIN_MENU_COUNT; i++) {
        int16_t y = listRowY(i) + 8;
        bool sel = (i == adminMenuItem);
        uint16_t rowBg = sel ? COL_BTN_BG : COL_BG;
        tft.fillRect(UI_MARGIN, y, TFT_WIDTH - UI_MARGIN * 2, LIST_ROW_H - 4, rowBg);
        if (sel) tft.drawRect(UI_MARGIN, y, TFT_WIDTH - UI_MARGIN * 2, LIST_ROW_H - 4, COL_ACCENT);
        tft.setTextSize(2);
        tft.setTextColor(sel ? COL_ACCENT : COL_LABEL, rowBg);
        tft.setCursor(UI_MARGIN + 12, y + (LIST_ROW_H - 4 - 16) / 2);
        tft.print(items[i]);
        addBtn(B_ROW0 + i, UI_MARGIN, y, TFT_WIDTH - UI_MARGIN * 2, LIST_ROW_H - 4,
               nullptr, COL_ACCENT, true, sel);
    }
    addBarBtn(B_EXIT, 0, 1, "EXIT", COL_LABEL);
    drawAllBtns();
}

void drawAdminWeightsScreen() {
    drawHeader("USER WEIGHTS");

    int total = USER_COUNT + 1;
    int vis   = min(listVisibleRows(), total);
    if (adminEditUser < adminWeightScroll) adminWeightScroll = adminEditUser;
    if (adminEditUser >= adminWeightScroll + vis) adminWeightScroll = adminEditUser - vis + 1;

    beginBtns();
    for (int row = 0; row < vis; row++) {
        int i = adminWeightScroll + row;
        if (i >= total) break;
        bool isGuest = (i == USER_COUNT);
        const char* name = isGuest ? "Guest" : userNames[i];
        int16_t y = listRowY(row);
        bool sel = (i == adminEditUser);

        uint16_t rowBg = sel ? COL_BTN_BG : COL_BG;
        tft.fillRect(UI_MARGIN, y, TFT_WIDTH - UI_MARGIN * 2, LIST_ROW_H - 4, rowBg);
        if (sel) tft.drawRect(UI_MARGIN, y, TFT_WIDTH - UI_MARGIN * 2, LIST_ROW_H - 4, COL_ACCENT);

        tft.setTextSize(2);
        tft.setTextColor(sel ? COL_ACCENT : COL_LABEL, rowBg);
        tft.setCursor(UI_MARGIN + 12, y + (LIST_ROW_H - 4 - 16) / 2);
        tft.print(name);

        tft.setTextColor(sel ? COL_VALUE : COL_DIVIDER, rowBg);
        tft.setCursor(TFT_WIDTH - UI_MARGIN - 68, y + (LIST_ROW_H - 4 - 16) / 2);
        tft.printf("%3.0f", userWeights[i]);
        tft.setTextSize(1);
        tft.print("kg");

        addBtn(B_ROW0 + row, UI_MARGIN, y, TFT_WIDTH - UI_MARGIN * 2, LIST_ROW_H - 4,
               nullptr, COL_ACCENT, true, sel);
    }
    addBarBtn(B_BACK, 0, 2, "BACK", COL_LABEL);
    addBarBtn(B_EDIT, 1, 2, "EDIT", COL_ACCENT);
    drawAllBtns();
}

void drawAdminWeightEditScreen() {
    bool isGuest = (adminEditUser == USER_COUNT);
    const char* name = isGuest ? "Guest" : userNames[adminEditUser];

    drawHeader("EDIT WEIGHT");

    tft.setTextSize(2);
    tft.setTextColor(COL_ACCENT, COL_BG);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 8);
    tft.print(name);

    char buf[16];
    snprintf(buf, sizeof(buf), "%3.0f", userWeights[adminEditUser]);
    tft.setTextSize(6);
    tft.setTextColor(COL_VALUE, COL_BG);
    tft.setCursor(30, CONTENT_Y + 44);
    tft.print(buf);
    tft.setTextSize(3);
    tft.print("kg");

    beginBtns();
    int16_t sy = CONTENT_Y + 120, sw = (TFT_WIDTH - UI_MARGIN * 5) / 4;
    addBtn(B_DEC_BIG, UI_MARGIN,                     sy, sw, 52, "-5", COL_ERROR);
    addBtn(B_DEC,     UI_MARGIN * 2 + sw,            sy, sw, 52, "-1", COL_LABEL);
    addBtn(B_INC,     UI_MARGIN * 3 + sw * 2,        sy, sw, 52, "+1", COL_LABEL);
    addBtn(B_INC_BIG, UI_MARGIN * 4 + sw * 3,        sy, sw, 52, "+5", COL_VALUE);
    addBarBtn(B_CANCEL, 0, 2, "CANCEL", COL_LABEL);
    addBarBtn(B_SAVE,   1, 2, "SAVE",   COL_VALUE);
    drawAllBtns();
}

// ───────── Numeric keypad (admin password entry) ─────────
// 4 rows must fit between the dots row and the bottom edge:
// KEY_Y0 + 4*KEY_H + 3*KEY_GAP = 92 + 208 + 15 = 315 < TFT_HEIGHT.
#define KEY_W   74
#define KEY_H   52
#define KEY_GAP 5
#define KEY_X0  ((TFT_WIDTH - KEY_W * 3 - KEY_GAP * 2) / 2)
#define KEY_Y0  (CONTENT_Y + 56)

void addKeypad(bool withEscape) {
    static const char* const digits[9] = {"1","2","3","4","5","6","7","8","9"};
    for (int i = 0; i < 9; i++) {
        int16_t x = KEY_X0 + (i % 3) * (KEY_W + KEY_GAP);
        int16_t y = KEY_Y0 + (i / 3) * (KEY_H + KEY_GAP);
        addBtn(B_KEY1 + i, x, y, KEY_W, KEY_H, digits[i], COL_TEXT);
    }
    int16_t y = KEY_Y0 + 3 * (KEY_H + KEY_GAP);
    addBtn(B_KEYDEL, KEY_X0,                        y, KEY_W, KEY_H, "DEL", COL_WARN);
    addBtn(B_KEY0,   KEY_X0 + KEY_W + KEY_GAP,      y, KEY_W, KEY_H, "0",   COL_TEXT);
    if (withEscape)
        addBtn(B_KEYESC, KEY_X0 + (KEY_W + KEY_GAP) * 2, y, KEY_W, KEY_H, "ESC", COL_ERROR);
}

// Row of dots showing how many digits are entered.
void drawPwdDots(int16_t y) {
    int16_t step = 34, x0 = (TFT_WIDTH - step * CALIB_PASSWORD_LEN) / 2;
    for (int i = 0; i < CALIB_PASSWORD_LEN; i++) {
        tft.setTextSize(3);
        tft.setTextColor(i < (int)pwdPos ? COL_ACCENT : COL_DIVIDER, COL_BG);
        tft.setCursor(x0 + i * step + 8, y);
        tft.print(i < (int)pwdPos ? "*" : "-");
    }
}

void drawCalibAuthScreen() {
    drawHeader("ADMIN ACCESS");

    tft.setTextSize(1);
    tft.setTextColor(pwdWrong ? COL_ERROR : COL_LABEL, COL_BG);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 6);
    tft.print(pwdWrong ? "WRONG PASSWORD - try again" : "Enter password:      ");

    drawPwdDots(CONTENT_Y + 24);

    beginBtns();
    addKeypad(true);
    drawAllBtns();
}

void drawAdminPwdEditScreen() {
    drawHeader("SET PASSWORD");

    tft.setTextSize(1);
    tft.setTextColor(pwdConfirmPhase ? COL_WARN : COL_LABEL, COL_BG);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 6);
    tft.print(pwdConfirmPhase ? "Re-enter to confirm: " : "Enter NEW password:  ");

    drawPwdDots(CONTENT_Y + 24);

    beginBtns();
    addKeypad(true);
    drawAllBtns();
}

void drawCalibScreen() {
    drawHeader("CALIBRATION");

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 6);
    tft.print("METERS / PULSE");

    tft.setTextSize(3);
    tft.setTextColor(COL_VALUE, COL_BG);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 22);
    tft.printf("%.6f", calMetersPerPulse);

    tft.setTextSize(2);
    tft.setTextColor(COL_ACCENT, COL_BG);
    tft.setCursor(UI_MARGIN, CONTENT_Y + 56);
    tft.printf("= %.4f m/rev ", calMetersPerPulse * PULSES_PER_REV);

    drawDivider(CONTENT_Y + 84);

    beginBtns();
    int16_t sy = CONTENT_Y + 94, sw = (TFT_WIDTH - UI_MARGIN * 5) / 4;
    addBtn(B_DEC_BIG, UI_MARGIN,              sy, sw, 52, "-10", COL_ERROR);
    addBtn(B_DEC,     UI_MARGIN * 2 + sw,     sy, sw, 52, "-1",  COL_LABEL);
    addBtn(B_INC,     UI_MARGIN * 3 + sw * 2, sy, sw, 52, "+1",  COL_LABEL);
    addBtn(B_INC_BIG, UI_MARGIN * 4 + sw * 3, sy, sw, 52, "+10", COL_VALUE);

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(UI_MARGIN, sy + 62);
    tft.print("step = 0.000001 m/pulse");
    tft.setTextColor(COL_WARN, COL_BG);
    tft.setCursor(UI_MARGIN, sy + 76);
    tft.printf("Factory default: %.6f", (float)METERS_PER_PULSE);

    addBarBtn(B_CANCEL, 0, 2, "CANCEL", COL_LABEL);
    addBarBtn(B_SAVE,   1, 2, "SAVE",   COL_VALUE);
    drawAllBtns();
}

// ───────── Save to history ─────────
void saveToHistory(uint32_t durSec) {
    if (historyCount < MAX_HISTORY) historyCount++;
    for (int i = historyCount - 1; i > 0; i--)
        history[i] = history[i - 1];
    String ts = getTimestamp();
    strncpy(history[0].date, ts.c_str(), sizeof(history[0].date) - 1);
    history[0].durationSec = durSec;
    history[0].distance    = totalMeters;
    history[0].strokes     = strokeCount;
    history[0].calories    = totalCalories;
}

// ───────── Reset ─────────
void resetWorkout() {
    totalPulses = 0; totalMeters = 0; totalCalories = 0;
    strokeCount = 0; currentSpeed = 0; strokeRate = 0;
    workoutElapsedMs = 0; lastPulseMs = 0;
    lastStrokeBoundary = 0; inDrive = false;
    emaIntervalMs = 0; extremaInterval = 0; emaSpeed = 0;
    strokeTsHead = 0; strokeTsCount = 0;
    kEff = 0; recoveryStartMs = 0; recoveryStartInterval = 0;
    pulseCount = 0; displayPage = 0;
    lastPulseUsSnap = 0; omegaMarkUs = 0; omegaMarkPulses = 0;
#ifdef SENSOR_TYPE_LDR
    ldrPulseActive = false;
    ldrPulseCount = 0; ldrLastPulseUs = 0; ldrPulseIntervalUs = 0;
#endif
#ifdef SENSOR_TYPE_HALL
    hallPulseCount = 0;
#endif
#ifdef SENSOR_TYPE_BLOCKER
    blockerPulseCount = 0;
#endif
}

// ───────── SPM over a trailing window (>= SPM_WINDOW_MS) ─────────
// Walk back through recorded stroke times until the span covers the window
// (or the ring is exhausted); rate = intervals / span. Averages >= 10 s.
float computeSpm() {
    if (strokeTsCount < 2) return strokeRate;   // need >= 2 strokes for an interval
    int newest = (strokeTsHead - 1 + STROKE_TS_MAX) % STROKE_TS_MAX;
    unsigned long tNew = strokeTimes[newest];
    unsigned long tOld = tNew;
    int n = 1;
    for (int k = 1; k < strokeTsCount; k++) {
        int j = (newest - k + STROKE_TS_MAX) % STROKE_TS_MAX;
        tOld = strokeTimes[j];
        n++;
        if (tNew - tOld >= SPM_WINDOW_MS) break;   // window covered
    }
    if (tNew <= tOld) return strokeRate;
    return (float)(n - 1) * 60000.0f / (float)(tNew - tOld);
}

// ───────── Process sensor data ─────────
void processSensor() {
    if (!workoutActive) return;

    if (pulseCount > totalPulses) {
        uint32_t newPulses = pulseCount - totalPulses;
        totalPulses = pulseCount;
        totalMeters += newPulses * calMetersPerPulse;
        // calories: weight-based estimate (~0.571 kcal per kg per km)
        totalCalories = totalMeters * userWeights[currentUser] * 0.000571f;

        unsigned long now = millis();

        // ── ω estimate: average pulse interval over a ¼-rev window ──
        // One clean sample per OMEGA_AVG_PULSES pulses; slot-geometry noise
        // (uneven disc slots) averages out across the window instead of
        // rattling the stroke detector at 60x the rev rate.
        #define MAX_VALID_INTERVAL_MS 1000.0f   // < 1 rev/s: flywheel effectively stopped
        if (omegaMarkUs == 0) {
            omegaMarkUs     = lastPulseUsSnap;   // seed window at first pulse
            omegaMarkPulses = pulseCount;
        } else if (pulseCount - omegaMarkPulses >= OMEGA_AVG_PULSES) {
            uint32_t n = pulseCount - omegaMarkPulses;
            float avgIv = (float)(lastPulseUsSnap - omegaMarkUs) / 1000.0f / n;
            omegaMarkUs     = lastPulseUsSnap;
            omegaMarkPulses = pulseCount;

            if (avgIv > 0 && avgIv < MAX_VALID_INTERVAL_MS) {
                emaIntervalMs = (emaIntervalMs > 0) ? (0.5f * emaIntervalMs + 0.5f * avgIv) : avgIv;

                // Boat speed from smoothed angular velocity (omega ~ 1/interval), then smooth again
                float spd = calMetersPerPulse / (emaIntervalMs / 1000.0f);
                emaSpeed = (emaSpeed > 0) ? (0.7f * emaSpeed + 0.3f * spd) : spd;
                currentSpeed = emaSpeed;
            }
        }

        // ── Stroke detection by flywheel accel/decel (peak/valley of omega) ──
        //   interval shrinking -> flywheel speeding up -> DRIVE (pull)
        //   interval growing   -> flywheel coasting    -> RECOVERY
        // A new stroke = recovery->drive reversal (interval turns from rising to falling).
        if (emaIntervalMs > 0) {
            if (!inDrive) {
                // recovery: track the max interval; a drop past hysteresis = drive start
                if (emaIntervalMs > extremaInterval) extremaInterval = emaIntervalMs;
                if (extremaInterval > 0 &&
                    emaIntervalMs < extremaInterval * (1.0f - STROKE_HYST) &&
                    (now - lastStrokeBoundary > STROKE_MIN_MS)) {
                    inDrive = true;

                    // estimate drag k_eff from the recovery that just ended:
                    // during recovery 1/omega (proportional to interval) grows ~linearly,
                    // slope = k/I  ->  k = I * (N/2pi) * d(interval_s)/dt_s
                    if (recoveryStartMs > 0) {
                        float dt_s  = (now - recoveryStartMs) / 1000.0f;
                        float dIv_s = (extremaInterval - recoveryStartInterval) / 1000.0f;
                        if (dt_s > 0.2f && dIv_s > 0.0f) {
                            float kInst = FLYWHEEL_INERTIA * (PULSES_PER_REV / 6.2831853f)
                                          * (dIv_s / dt_s);
                            kEff = (kEff > 0) ? (0.7f * kEff + 0.3f * kInst) : kInst;
                        }
                    }

                    lastStrokeBoundary = now;
                    strokeCount++;
                    extremaInterval = emaIntervalMs;   // now track the min during drive

                    // record stroke time; SPM = average over >= SPM_WINDOW_MS
                    strokeTimes[strokeTsHead] = now;
                    strokeTsHead = (strokeTsHead + 1) % STROKE_TS_MAX;
                    if (strokeTsCount < STROKE_TS_MAX) strokeTsCount++;
                    strokeRate = computeSpm();
                }
            } else {
                // drive: track the min interval; a rise past hysteresis = recovery start.
                // Require a minimum drive duration: a noise blip flipping to "recovery"
                // early in the drive would make the rest of the drive count as a second
                // stroke (double counting).
                #ifndef STROKE_MIN_DRIVE_MS
                #define STROKE_MIN_DRIVE_MS 250
                #endif
                if (emaIntervalMs < extremaInterval || extremaInterval == 0) extremaInterval = emaIntervalMs;
                if (emaIntervalMs > extremaInterval * (1.0f + STROKE_HYST) &&
                    (now - lastStrokeBoundary > STROKE_MIN_DRIVE_MS)) {
                    inDrive = false;
                    recoveryStartMs = now;
                    recoveryStartInterval = emaIntervalMs;
                    extremaInterval = emaIntervalMs;   // now track the max during recovery
                }
            }
        }
    }

    // Idle detection: no pulses for a while -> stop dynamics (distance & count are kept)
    if (lastPulseMs > 0 && (millis() - lastPulseMs > IDLE_TIMEOUT_MS)) {
        currentSpeed = 0;
        strokeRate = 0;
        emaSpeed = 0;
        emaIntervalMs = 0;
        extremaInterval = 0;
        inDrive = false;
        lastStrokeBoundary = 0;   // next stroke after idle won't compute a bogus SPM
        strokeTsHead = 0; strokeTsCount = 0;
        recoveryStartMs = 0;      // don't measure drag across a pause
        omegaMarkUs = 0;          // restart the ω window: don't average across the gap
    }
}

// ───────── Admin entry ─────────
void enterAdmin() {
    pwdPos = 0; pwdWrong = false;
    beep(); delay(80); beep();
#if CALIB_REQUIRE_PASSWORD
    currentScreen = SCR_CALIB_AUTH;
#else
    adminMenuItem = 0;
    currentScreen = SCR_ADMIN_MENU;   // no password: open admin directly
#endif
}

// Feeds one keypad digit into pwdBuffer; returns true when the buffer filled.
bool pwdPushDigit(uint8_t digit) {
    beep();
    pwdWrong = false;
    if (pwdPos < CALIB_PASSWORD_LEN) pwdBuffer[pwdPos++] = digit;
    return pwdPos >= CALIB_PASSWORD_LEN;
}

// Maps a keypad button id to its digit, or -1 if it is not a digit key.
int keyDigit(uint8_t id) {
    if (id >= B_KEY0 && id <= B_KEY9) return id - B_KEY0;
    return -1;
}

// ───────── Handle input ─────────
void handleInput() {
    uint8_t hit = tappedBtn();

    switch (currentScreen) {
    case SCR_IDLE:
        // Hold anywhere on the title bar for ADMIN_HOLD_MS to open admin.
        if (touch.hold) {
            touch.hold = false;
            if (touch.downY < HEADER_H) { uiDirty = true; enterAdmin(); return; }
        }
        switch (hit) {
        case B_BRIGHT_UP: brightness = min(255, brightness + 25); setBacklight(brightness); break;
        case B_BRIGHT_DN: brightness = max((uint8_t)10, (uint8_t)(brightness - 25)); setBacklight(brightness); break;
        case B_HISTORY:   beep(); displayPage = 0; currentScreen = SCR_HISTORY; break;
        case B_START:     beep(); currentUser = 0; userScrollOffset = 0;
                          currentScreen = SCR_USER_SELECT; break;
        }
        break;

    case SCR_USER_SELECT: {
        int total = USER_COUNT + 1;
        int vis   = min(listVisibleRows(), total);
        if (touch.swipe) {                       // swipe scrolls the list
            userScrollOffset = constrain(userScrollOffset + touch.swipe * vis,
                                         0, max(0, total - vis));
            currentUser = constrain(currentUser, userScrollOffset,
                                    min(total - 1, userScrollOffset + vis - 1));
            touch.swipe = 0;
            uiDirty = true;
            break;
        }
        if (hit >= B_ROW0 && hit <= B_ROW8) {
            int i = userScrollOffset + (hit - B_ROW0);
            if (i < total) { beep(); currentUser = i; }
        } else if (hit == B_BACK) {
            beep(); currentScreen = SCR_IDLE;
        } else if (hit == B_START) {
            beep();
            resetWorkout();
            workoutActive = true;
            workoutStartMs = millis();
            currentScreen = SCR_WORKOUT;
            playTone(TONE_START, TONE_DURATION);
        }
        break;
    }

    case SCR_WORKOUT:
        if (hit == B_PAUSE) {
            workoutElapsedMs += millis() - workoutStartMs;
            workoutActive = false;
            currentScreen = SCR_PAUSED;
            playTone(TONE_STOP, TONE_DURATION);
        }
        break;

    case SCR_PAUSED:
        if (hit == B_RESUME) {
            beep();
            workoutActive = true;
            workoutStartMs = millis();
            currentScreen = SCR_WORKOUT;
            playTone(TONE_START, TONE_DURATION);
        } else if (hit == B_FINISH) {
            beep();
            currentScreen = SCR_UPLOADING;
        }
        break;

    case SCR_SUMMARY:
        if (hit == B_OK) { beep(); currentScreen = SCR_IDLE; }
        break;

    case SCR_HISTORY:
        if (touch.swipe) {
            displayPage = constrain(displayPage + touch.swipe, 0, max(0, historyCount - 1));
            touch.swipe = 0;
            uiDirty = true;
            break;
        }
        if (hit == B_PREV)      { if (displayPage > 0) displayPage--; }
        else if (hit == B_NEXT) { if (displayPage < historyCount - 1) displayPage++; }
        else if (hit == B_BACK) { beep(); currentScreen = SCR_IDLE; }
        break;

    case SCR_CALIB_AUTH: {
        int d = keyDigit(hit);
        if (d >= 0) {
            if (pwdPushDigit((uint8_t)d)) {
                bool ok = true;
                for (int i = 0; i < CALIB_PASSWORD_LEN; i++)
                    if (pwdBuffer[i] != adminPwd[i]) { ok = false; break; }
                pwdPos = 0;
                if (ok) {
                    playTone(1600, 100);
                    adminMenuItem = 0;
                    currentScreen = SCR_ADMIN_MENU;
                } else {
                    pwdWrong = true;
                    playTone(200, 300);
                }
            }
        } else if (hit == B_KEYDEL) {
            beep(); if (pwdPos > 0) pwdPos--;
        } else if (hit == B_KEYESC) {
            beep(); pwdPos = 0; pwdWrong = false; currentScreen = SCR_IDLE;
        }
        break;
    }

    case SCR_ADMIN_MENU:
        if (hit >= B_ROW0 && hit < B_ROW0 + ADMIN_MENU_COUNT) {
            beep();
            adminMenuItem = hit - B_ROW0;
            if (adminMenuItem == 0) currentScreen = SCR_CALIB;
            else if (adminMenuItem == 1) { adminEditUser = 0; currentScreen = SCR_ADMIN_WEIGHTS; }
            else {
                pwdPos = 0; pwdConfirmPhase = 0;
                pwdEditLastKeyMs = millis();
                currentScreen = SCR_ADMIN_PWD_EDIT;
            }
        } else if (hit == B_EXIT) {
            beep(); currentScreen = SCR_IDLE;
        }
        break;

    case SCR_CALIB:
        switch (hit) {
        case B_INC:     calMetersPerPulse += 0.000001f; break;
        case B_INC_BIG: calMetersPerPulse += 0.000010f; break;
        case B_DEC:     if (calMetersPerPulse > 0.000001f) calMetersPerPulse -= 0.000001f; break;
        case B_DEC_BIG: calMetersPerPulse = max(0.000001f, calMetersPerPulse - 0.000010f); break;
        case B_SAVE:
            saveCalibration();
            playTone(TONE_UPLOAD, 200);
            currentScreen = SCR_ADMIN_MENU;
            break;
        case B_CANCEL:
            loadCalibration();
            beep();
            currentScreen = SCR_ADMIN_MENU;
            break;
        }
        break;

    case SCR_ADMIN_WEIGHTS: {
        int total = USER_COUNT + 1;
        if (touch.swipe) {
            int vis = min(listVisibleRows(), total);
            adminEditUser = constrain(adminEditUser + touch.swipe * vis, 0, total - 1);
            touch.swipe = 0;
            uiDirty = true;
            break;
        }
        if (hit >= B_ROW0 && hit <= B_ROW8) {
            int i = adminWeightScroll + (hit - B_ROW0);
            if (i < total) {
                beep();
                // second tap on the already-selected row opens the editor
                if (i == adminEditUser) currentScreen = SCR_ADMIN_WEIGHT_EDIT;
                else adminEditUser = i;
            }
        } else if (hit == B_BACK) {
            beep(); currentScreen = SCR_ADMIN_MENU;
        } else if (hit == B_EDIT) {
            beep(); currentScreen = SCR_ADMIN_WEIGHT_EDIT;
        }
        break;
    }

    case SCR_ADMIN_WEIGHT_EDIT:
        switch (hit) {
        case B_INC:     userWeights[adminEditUser] = min(200.0f, userWeights[adminEditUser] + 1.0f); break;
        case B_INC_BIG: userWeights[adminEditUser] = min(200.0f, userWeights[adminEditUser] + 5.0f); break;
        case B_DEC:     userWeights[adminEditUser] = max(20.0f,  userWeights[adminEditUser] - 1.0f); break;
        case B_DEC_BIG: userWeights[adminEditUser] = max(20.0f,  userWeights[adminEditUser] - 5.0f); break;
        case B_SAVE:
            saveWeights();
            playTone(TONE_UPLOAD, 200);
            currentScreen = SCR_ADMIN_WEIGHTS;
            break;
        case B_CANCEL:
            loadCalibration();   // revert weights from NVS
            beep();
            currentScreen = SCR_ADMIN_WEIGHTS;
            break;
        }
        break;

    case SCR_ADMIN_PWD_EDIT: {
        int d = keyDigit(hit);
        if (d >= 0) {
            pwdEditLastKeyMs = millis();
            if (pwdPushDigit((uint8_t)d)) {
                pwdPos = 0;
                if (pwdConfirmPhase == 0) {
                    memcpy(pwdNew, pwdBuffer, CALIB_PASSWORD_LEN);
                    pwdConfirmPhase = 1;
                } else if (memcmp(pwdNew, pwdBuffer, CALIB_PASSWORD_LEN) == 0) {
                    memcpy(adminPwd, pwdNew, CALIB_PASSWORD_LEN);
                    saveAdminPwd();
                    playTone(TONE_UPLOAD, 200);   // saved
                    currentScreen = SCR_ADMIN_MENU;
                } else {
                    playTone(200, 300);           // mismatch: start over
                    pwdConfirmPhase = 0;
                }
            }
        } else if (hit == B_KEYDEL) {
            beep(); pwdEditLastKeyMs = millis(); if (pwdPos > 0) pwdPos--;
        } else if (hit == B_KEYESC) {
            beep(); currentScreen = SCR_ADMIN_MENU;   // cancel, keep old password
        }
        if (millis() - pwdEditLastKeyMs > PWD_EDIT_TIMEOUT_MS) {
            beep();                                   // idle timeout = cancel
            uiDirty = true;
            currentScreen = SCR_ADMIN_MENU;
        }
        break;
    }

    default: break;
    }

    // Any unconsumed gesture must not leak into the next screen.
    touch.tap = false;
    touch.hold = false;
    touch.swipe = 0;
}

// ───────── Display refresh ─────────
unsigned long lastRedraw = 0;
#define REDRAW_MS 1000
Screen lastDrawnScreen = (Screen)-1;

void drawCurrentScreen() {
    switch (currentScreen) {
        case SCR_IDLE:        drawIdleScreen(); break;
        case SCR_USER_SELECT: drawUserSelectScreen(); break;
        case SCR_WORKOUT:     drawWorkoutScreen(); break;
        case SCR_PAUSED:      drawPausedScreen(); break;
        case SCR_HISTORY:     drawHistoryScreen(); break;
        case SCR_CALIB_AUTH:        drawCalibAuthScreen(); break;
        case SCR_ADMIN_MENU:        drawAdminMenuScreen(); break;
        case SCR_CALIB:             drawCalibScreen(); break;
        case SCR_ADMIN_WEIGHTS:     drawAdminWeightsScreen(); break;
        case SCR_ADMIN_WEIGHT_EDIT: drawAdminWeightEditScreen(); break;
        case SCR_ADMIN_PWD_EDIT:    drawAdminPwdEditScreen(); break;
        default: break;
    }
}

void refreshDisplay() {
    bool screenChanged = (currentScreen != lastDrawnScreen);
    // Full clear when entering a screen or after a touch changed UI state.
    // Periodic redraws (workout timer, idle sensor value) repaint in place
    // with background-colored text + targeted fillRects — no clear, no flicker.
    bool needsClear = screenChanged || uiDirty;

    // Screens with live data; everything else redraws only on input/transition
    bool needsPeriodicRefresh = (currentScreen == SCR_WORKOUT || currentScreen == SCR_PAUSED ||
                                 currentScreen == SCR_IDLE);

    if (!needsClear && (!needsPeriodicRefresh || millis() - lastRedraw < REDRAW_MS)) {
        // Nothing to repaint except the press highlight on the current buttons.
        if (btnDirty) { btnDirty = false; drawAllBtns(); }
        return;
    }
    lastRedraw = millis();
    uiDirty = false;
    btnDirty = false;
    if (needsClear) {
        tft.fillScreen(COL_BG);
        lastDrawnScreen = currentScreen;
    }

    drawCurrentScreen();
}

// ───────── Setup ─────────
void setup() {
    Serial.begin(115200);
    // Serial rides on USB CDC here; without a moment to enumerate, every log
    // line printed during init is lost — including the touch diagnostics.
    delay(400);

    // Backlight (LEDC channel 0)
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL, 0);
    setBacklight(0);          // stay dark until the panel has something to show

    // TFT on hardware SPI2
    tftSPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);
    tft.init(TFT_WIDTH, TFT_HEIGHT);
    tft.setSPISpeed(TFT_SPI_HZ);
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(COL_BG);
    tft.setTextWrap(false);
    setBacklight(brightness);

    // Touch panel. It is the only input on this board, so a failure has to be
    // visible on the panel itself — otherwise the device just looks frozen.
    bool touchOk = touchInit();
    if (!touchOk) {
        Serial.println("[TOUCH] CST328 init failed — UI will not respond");
        drawCentered(120, "TOUCH INIT FAILED", 2, COL_ERROR);
        drawCentered(150, "check the CST328 ribbon", 1, COL_LABEL);
        delay(2500);
        tft.fillScreen(COL_BG);
    }

    // Audio (I2S -> PCM5101)
    initAudio();

    // Splash: waves rise from the bottom, then the rower and the title land
    // above them (220..260 waves, 170..218 rower, 40..148 text — no overlap).
    for (int y = TFT_HEIGHT - 60; y >= TFT_HEIGHT / 2 + 60; y -= 8) {
        for (int x = 0; x < TFT_WIDTH; x += 32)
            drawIcon(x, y, bmp_wave, BLUE, 2);
        delay(40);
    }

    drawIcon((TFT_WIDTH - 48) / 2, 170, bmp_rower, COL_ACCENT, 3);
    delay(200);

    drawCentered(40, "WATER", 4, COL_ACCENT);
    drawCentered(78, "ROWER", 4, COL_ACCENT);
    drawCentered(120, "v3.0  touch edition", 1, COL_VALUE);

#ifdef SENSOR_TYPE_LDR
    drawCentered(140, "Sensor: LDR", 1, COL_LABEL);
#elif defined(SENSOR_TYPE_HALL)
    drawCentered(140, "Sensor: Hall", 1, COL_LABEL);
#elif defined(SENSOR_TYPE_BLOCKER)
    drawCentered(140, "Sensor: Blocker", 1, COL_LABEL);
#endif

    // Sensor pins
#ifdef SENSOR_TYPE_LDR
    pinMode(LDR_SIGNAL_PIN, INPUT);
    analogSetAttenuation(ADC_11db);  // full 0-3.3V range
    // 1 kHz timer-driven sampling (immune to loop/redraw stalls); see ldrSampleISR
    ldrTimer = timerBegin(0, 80, true);              // 80 MHz / 80 = 1 MHz tick
    timerAttachInterrupt(ldrTimer, &ldrSampleISR, true);
    timerAlarmWrite(ldrTimer, 1000, true);           // 1000 us -> 1 kHz
    timerAlarmEnable(ldrTimer);
#endif
#ifdef SENSOR_TYPE_HALL
    pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_SENSOR_PIN), hallISR, FALLING);
#endif
#ifdef SENSOR_TYPE_BLOCKER
    pinMode(BLOCKER_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BLOCKER_SENSOR_PIN), blockerISR,
                    BLOCKER_ACTIVE_LOW ? FALLING : RISING);
#endif

    // Load saved calibration + admin password from NVS
    loadCalibration();
    loadAdminPwd();

    // BLE FTMS
    initBLE();

    // WiFi + NTP
    connectWiFi();
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    // Startup sound
    playTone(800, 100);
    playTone(1200, 100);
    playTone(1600, 150);

    delay(600);
    currentScreen = SCR_IDLE;
}

// ───────── Loop ─────────
void loop() {
    pollTouch();
    handleInput();

    // Read sensor (LDR: timer ISR / Hall & blocker: edge ISR updates globals)
    readSensor();

    processSensor();

    // BLE broadcast
    updateBLE();

    // Upload (blocking)
    if (currentScreen == SCR_UPLOADING) {
        uint32_t durSec = workoutElapsedMs / 1000;
        saveToHistory(durSec);
        bool ok = false;
        if (currentUser == USER_COUNT) {
            // Guest: screen + BLE only, no NAS upload
            beep();
        } else {
            drawUploadingScreen();
            ok = uploadToNAS(durSec);
            if (ok) {
                playTone(TONE_UPLOAD, 200);
            } else {
                playTone(300, 500);
            }
        }
        drawSummaryScreen(ok);
        currentScreen = SCR_SUMMARY;
        lastDrawnScreen = SCR_SUMMARY;
        uiDirty = false;   // summary drawn directly; a stale flag would blank it next frame
        btnDirty = false;
        return;
    }

    refreshDisplay();
}
