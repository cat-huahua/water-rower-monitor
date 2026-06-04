/*
 * Water Rower Monitor — ESP32-S3
 * Replacement console for Water Rower USA (SN 132224)
 *
 * Hardware:
 *   - ESP32-S3 DevKitC-1
 *   - ST7735S 1.8" 128x160 TFT with 4 built-in keys
 *   - Sensor: LDR (光敏电阻) OR Hall effect (霍尔传感器)
 *     Choose in config.h: SENSOR_TYPE_LDR or SENSOR_TYPE_HALL
 *   - Speaker (喇叭) — for sound feedback
 *   - Large half breadboard (165x55mm)
 *
 * Sensor options:
 *   LDR:  Original WaterRower optical sensor (analog read)
 *   Hall: A3144/OH3144 + magnet on flywheel (digital interrupt)
 *
 * Keys (on TFT module):
 *   * (K4)  = Start / Resume
 *   # (K3)  = Stop & save / Back
 *   UP (K1) = Scroll up / Brightness+
 *   DN (K2) = Scroll down / Brightness-
 */

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <ArduinoJson.h>
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
    "Calories won't burn\nthemselves",
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
#define BLUE          0x1C9F  // water blue

// ───────── Display ─────────
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
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
    if (calMetersPerPulse < 0.002f || calMetersPerPulse > 0.1f)
        calMetersPerPulse = METERS_PER_PULSE;
    for (int i = 0; i <= USER_COUNT; i++) {
        char key[6]; snprintf(key, sizeof(key), "w%d", i);
        userWeights[i] = prefs.getFloat(key, 70.0f);
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

// ───────── Combo / Password detector ─────────
static const uint8_t calibCombo[]    = CALIB_COMBO;
static const uint8_t calibPassword[] = CALIB_PASSWORD;

uint8_t comboBuffer[CALIB_COMBO_LEN] = {};
uint8_t comboPos = 0;
uint8_t pwdBuffer[CALIB_PASSWORD_LEN] = {};
uint8_t pwdPos   = 0;
bool    pwdWrong = false;

void pushCombo(uint8_t btn) {
    // Shift buffer
    for (int i = 0; i < CALIB_COMBO_LEN - 1; i++) comboBuffer[i] = comboBuffer[i+1];
    comboBuffer[CALIB_COMBO_LEN - 1] = btn;
    comboPos++;
}
bool checkCombo() {
    if (comboPos < CALIB_COMBO_LEN) return false;
    for (int i = 0; i < CALIB_COMBO_LEN; i++)
        if (comboBuffer[i] != calibCombo[i]) return false;
    return true;
}

// ───────── Admin state ─────────
int adminMenuItem  = 0;   // selected menu item
int adminEditUser  = 0;   // user being edited in weight screen
#define ADMIN_MENU_COUNT 2

// ───────── State machine ─────────
enum Screen { SCR_IDLE, SCR_USER_SELECT, SCR_WORKOUT, SCR_PAUSED, SCR_SUMMARY,
              SCR_UPLOADING, SCR_HISTORY,
              SCR_CALIB_AUTH, SCR_ADMIN_MENU, SCR_CALIB,
              SCR_ADMIN_WEIGHTS, SCR_ADMIN_WEIGHT_EDIT };
Screen currentScreen = SCR_IDLE;
int displayPage = 0;

// ───────── Sensor State ─────────
uint32_t pulseCount     = 0;
unsigned long lastPulseMs = 0;
unsigned long lastPulseIntervalMs = 0;

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

// ───────── Buttons ─────────
struct Button {
    uint8_t pin;
    bool    lastState;
    unsigned long lastDebounce;
    bool    pressed;
};
Button btnUp   = {BTN_UP_PIN,   true, 0, false};
Button btnDown = {BTN_DOWN_PIN, true, 0, false};
Button btnHash = {BTN_HASH_PIN, true, 0, false};
Button btnStar = {BTN_STAR_PIN, true, 0, false};
#define DEBOUNCE_MS 180

void readButton(Button &b) {
    bool state = digitalRead(b.pin);
    if (state != b.lastState && (millis() - b.lastDebounce > DEBOUNCE_MS)) {
        b.lastDebounce = millis();
        if (state == LOW) b.pressed = true;
    }
    b.lastState = state;
}

void readAllButtons() {
    readButton(btnUp);
    readButton(btnDown);
    readButton(btnHash);
    readButton(btnStar);
}

bool consume(Button &b) {
    if (b.pressed) { b.pressed = false; return true; }
    return false;
}

// ───────── Speaker ─────────
void playTone(uint16_t freq, uint16_t durationMs) {
    ledcWriteTone(1, freq);
    delay(durationMs);
    ledcWriteTone(1, 0);
}

void beep() {
    playTone(TONE_BEEP, 50);
}

// ───────── Backlight ─────────
void setBacklight(uint8_t val) {
    if (TFT_BL >= 0) {
        ledcWrite(0, val);
    }
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
    unsigned long interval = ldrPulseIntervalUs;
    interrupts();

    if (pc > pulseCount) {
        unsigned long now = millis();
        if (lastPulseMs > 0) {
            lastPulseIntervalMs = (interval > 0) ? interval / 1000 : (now - lastPulseMs);
        }
        lastPulseMs = now;
        pulseCount = pc;
    }
}

int getSensorRaw() { return analogRead(LDR_SIGNAL_PIN); }
const char* getSensorLabel() { return "LDR"; }
int getSensorThreshold() { return LDR_THRESHOLD; }
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
    unsigned long interval = hallPulseIntervalUs;
    interrupts();

    if (pc > pulseCount) {
        unsigned long now = millis();
        if (lastPulseMs > 0) {
            lastPulseIntervalMs = (interval > 0) ? interval / 1000 : (now - lastPulseMs);
        }
        lastPulseMs = now;
        pulseCount = pc;
    }
}

int getSensorRaw() { return digitalRead(HALL_SENSOR_PIN) == LOW ? 0 : 4095; }
const char* getSensorLabel() { return "HALL"; }
int getSensorThreshold() { return 0; }
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
    unsigned long interval = blockerPulseIntervalUs;
    interrupts();

    if (pc > pulseCount) {
        unsigned long now = millis();
        if (lastPulseMs > 0) {
            lastPulseIntervalMs = (interval > 0) ? interval / 1000 : (now - lastPulseMs);
        }
        lastPulseMs = now;
        pulseCount = pc;
    }
}

int getSensorRaw() { return digitalRead(BLOCKER_SENSOR_PIN) == LOW ? 0 : 4095; }
const char* getSensorLabel() { return "BLKR"; }
int getSensorThreshold() { return 0; }
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
bool uploadToGitHub(uint32_t durSec) {
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
void drawHeader(const char* title) {
    tft.fillRect(0, 0, TFT_WIDTH, 18, COL_HEADER_BG);
    tft.setTextSize(1);
    tft.setTextColor(COL_ACCENT);
    tft.setCursor(4, 5);
    tft.print(title);
    // BLE indicator
    tft.setCursor(TFT_WIDTH - 36, 5);
    tft.setTextColor(bleClientConnected ? COL_VALUE : COL_LABEL);
    tft.print(bleClientConnected ? "BT" : "bt");
    // WiFi indicator
    tft.setCursor(TFT_WIDTH - 16, 5);
    tft.setTextColor(WiFi.status() == WL_CONNECTED ? COL_VALUE : COL_ERROR);
    tft.print(WiFi.status() == WL_CONNECTED ? "W+" : "W-");
}

void drawDivider(int y) {
    tft.drawFastHLine(4, y, TFT_WIDTH - 8, COL_DIVIDER);
}

void drawLabelValue(int x, int y, const char* label, const char* value, uint16_t valColor = COL_VALUE) {
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(x, y);
    tft.print(label);
    tft.setTextColor(valColor);
    tft.setCursor(x, y + 12);
    tft.setTextSize(2);
    tft.print(value);
}

void drawKeyHints(const char* up, const char* down, const char* hash, const char* star) {
    int y = TFT_HEIGHT - 11;
    tft.fillRect(0, y - 2, TFT_WIDTH, 13, COL_HEADER_BG);
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    if (up && up[0])   { tft.setCursor(2, y);   tft.printf("\x18%s", up); }
    if (down && down[0]){ tft.setCursor(36, y);  tft.printf("\x19%s", down); }
    if (hash && hash[0]){ tft.setCursor(68, y);  tft.printf("#%s", hash); }
    if (star && star[0]){ tft.setCursor(100, y); tft.printf("*%s", star); }
}

// ───────── Screens ─────────
void drawIdleScreen() {
    tft.fillScreen(COL_BG);
    drawHeader("WATER ROWER");

    // Rowing person icon
    tft.drawBitmap(8, 22, bmp_rower, 16, 16, COL_ACCENT);
    // Water waves
    tft.drawBitmap(28, 28, bmp_wave, 16, 16, BLUE);
    tft.drawBitmap(48, 28, bmp_wave, 16, 16, BLUE);
    tft.drawBitmap(68, 28, bmp_wave, 16, 16, BLUE);
    tft.drawBitmap(88, 28, bmp_wave, 16, 16, BLUE);
    tft.drawBitmap(108, 28, bmp_wave, 16, 16, BLUE);

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(30, 22);
    tft.print(MACHINE_SN);

    drawDivider(48);

    // Sensor value
    int sensorVal = getSensorRaw();
    tft.setCursor(4, 52);
    tft.setTextColor(COL_LABEL);
    tft.setTextSize(1);
    tft.printf("%s: %d", getSensorLabel(), sensorVal);

    tft.setCursor(4, 66);
    tft.setTextColor(COL_ACCENT);
    tft.setTextSize(2);
    tft.print("READY");

    // Random funny idle message
    tft.setTextSize(1);
    tft.setTextColor(COL_WARN);
    tft.setCursor(4, 88);
    tft.print(idle_msgs[millis() / 3000 % NUM_IDLE_MSGS]);

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 108);
    tft.print("* START  # HISTORY");

    drawKeyHints("BRT+", "BRT-", "HIST", "GO");
}

void drawWorkoutScreen() {
    tft.fillScreen(COL_BG);
    drawHeader("ROWING");

    unsigned long elapsed = workoutElapsedMs + (millis() - workoutStartMs);
    uint32_t sec = elapsed / 1000;
    uint32_t mn = sec / 60;
    uint32_t s  = sec % 60;

    // Animated rower icon (alternates position)
    int rowerX = 4 + (millis() / 400 % 3) * 2;
    tft.drawBitmap(rowerX, 21, bmp_rower, 16, 16, COL_ACCENT);

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mn, s);
    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(30, 24);
    tft.print(timeBuf);

    drawDivider(42);

    // Distance with fire icon when going fast
    char distBuf[16];
    snprintf(distBuf, sizeof(distBuf), "%5.0f", totalMeters);
    drawLabelValue(4, 46, "DISTANCE (m)", distBuf);

    if (currentSpeed > 2.5f) {
        tft.drawBitmap(108, 46, bmp_fire, 16, 16, ST77XX_RED);
    }

    char splitBuf[16];
    if (currentSpeed > 0.1f) {
        float secPer500 = 500.0f / currentSpeed;
        int sp_min = (int)(secPer500 / 60);
        int sp_sec = (int)secPer500 % 60;
        snprintf(splitBuf, sizeof(splitBuf), "%d:%02d", sp_min, sp_sec);
    } else {
        snprintf(splitBuf, sizeof(splitBuf), "--:--");
    }
    drawLabelValue(4, 80, "/500m SPLIT", splitBuf, COL_ACCENT);

    // Motivational message (changes every 10 seconds)
    drawDivider(103);
    tft.setTextSize(1);
    tft.setTextColor(COL_WARN);
    tft.setCursor(4, 106);
    tft.print(fun_msgs[(sec / 10) % NUM_FUN_MSGS]);

    // SPM & CAL
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 118);  tft.print("SPM");
    tft.setCursor(68, 118); tft.print("CAL");
    tft.setTextSize(2);
    tft.setTextColor(COL_VALUE);
    tft.setCursor(4, 130);  tft.printf("%-3.0f", strokeRate);
    tft.setCursor(68, 130); tft.printf("%.0f", totalCalories);

    // Animated water at bottom
    int waveOffset = (millis() / 300) % 16;
    for (int x = waveOffset - 16; x < 128; x += 16) {
        tft.drawBitmap(x, 148, bmp_wave, 16, 16, BLUE);
    }

    drawKeyHints("", "", "STOP", "");
}

void drawPausedScreen() {
    tft.fillScreen(COL_BG);
    drawHeader("PAUSED");

    uint32_t sec = workoutElapsedMs / 1000;
    uint32_t mn = sec / 60;
    uint32_t s  = sec % 60;

    // Skull icon - "are you dying?"
    tft.drawBitmap(4, 24, bmp_skull, 16, 16, COL_WARN);

    tft.setTextSize(2);
    tft.setTextColor(COL_WARN);
    tft.setCursor(28, 28);
    tft.print("PAUSED");

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(28, 43);
    tft.print("Tired already?");

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mn, s);
    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(30, 58);
    tft.print(timeBuf);

    drawDivider(78);

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 82);   tft.printf("Dist: %.0f m", totalMeters);
    tft.setCursor(4, 94);   tft.printf("Strokes: %d", strokeCount);
    tft.setCursor(4, 106);  tft.printf("Cal: %.0f", totalCalories);
    tft.setCursor(4, 118);  tft.printf("Drag k: %.4f", kEff);

    // Motivational nudge
    tft.setTextColor(COL_ACCENT);
    tft.setCursor(4, 130);
    tft.print("* = Get back in there!");

    drawKeyHints("", "", "SAVE", "GO!");
}

void drawSummaryScreen(bool uploadOk) {
    tft.fillScreen(COL_BG);
    drawHeader("SUMMARY");

    // Trophy icon
    tft.drawBitmap(4, 22, bmp_trophy, 16, 16, COL_WARN);

    uint32_t sec = workoutElapsedMs / 1000;
    uint32_t mn = sec / 60;
    uint32_t s  = sec % 60;
    char buf[32];

    snprintf(buf, sizeof(buf), "%02d:%02d", mn, s);
    drawLabelValue(24, 22, "TIME", buf, COL_TEXT);

    snprintf(buf, sizeof(buf), "%.0f", totalMeters);
    drawLabelValue(4, 52, "DISTANCE (m)", buf);

    snprintf(buf, sizeof(buf), "%d", strokeCount);
    drawLabelValue(4, 82, "STROKES", buf, COL_ACCENT);

    snprintf(buf, sizeof(buf), "%.0f", totalCalories);
    drawLabelValue(68, 82, "CAL", buf, COL_WARN);

    drawDivider(108);

    // Funny completion message
    tft.setTextSize(1);
    tft.setTextColor(COL_WARN);
    tft.setCursor(4, 112);
    tft.print(done_msgs[sec % NUM_DONE_MSGS]);

    // Upload status with emoji
    tft.setCursor(4, 126);
    if (currentUser == USER_COUNT) {
        tft.drawBitmap(4, 124, bmp_happy, 16, 16, COL_WARN);
        tft.setTextColor(COL_WARN);
        tft.setCursor(24, 128);
        tft.print("Guest - BT only");
    } else if (uploadOk) {
        tft.drawBitmap(4, 124, bmp_happy, 16, 16, COL_VALUE);
        tft.setTextColor(COL_VALUE);
        tft.setCursor(24, 128);
        tft.print("Saved to NAS!");
    } else {
        tft.drawBitmap(4, 124, bmp_sad, 16, 16, COL_ERROR);
        tft.setTextColor(COL_ERROR);
        tft.setCursor(24, 128);
        tft.print("Upload FAILED");
    }

    drawKeyHints("", "", "BACK", "");
}

void drawHistoryScreen() {
    tft.fillScreen(COL_BG);
    drawHeader("HISTORY");

    if (historyCount == 0) {
        tft.setTextSize(1);
        tft.setTextColor(COL_LABEL);
        tft.setCursor(20, 60);
        tft.print("No records yet");
    } else {
        int idx = displayPage;
        if (idx >= historyCount) idx = historyCount - 1;
        WorkoutRecord &r = history[idx];

        tft.setTextSize(1);
        tft.setTextColor(COL_ACCENT);
        tft.setCursor(4, 24);
        tft.printf("Record %d/%d", idx + 1, historyCount);
        tft.setTextColor(COL_LABEL);
        tft.setCursor(4, 38);
        tft.print(r.date);

        char buf[32];
        uint32_t mn = r.durationSec / 60;
        uint32_t s = r.durationSec % 60;
        snprintf(buf, sizeof(buf), "%02d:%02d", mn, s);
        drawLabelValue(4, 54, "TIME", buf, COL_TEXT);

        snprintf(buf, sizeof(buf), "%.0f", r.distance);
        drawLabelValue(4, 84, "DIST(m)", buf);

        snprintf(buf, sizeof(buf), "%d", r.strokes);
        drawLabelValue(68, 84, "STR", buf, COL_ACCENT);

        tft.setTextSize(1);
        tft.setTextColor(COL_LABEL);
        tft.setCursor(4, 118);
        tft.printf("Cal: %.0f", r.calories);
    }

    drawKeyHints("PREV", "NEXT", "BACK", "");
}

void drawUploadingScreen() {
    tft.fillScreen(COL_BG);
    drawHeader("SAVING...");

    // Animated dots loading effect
    tft.drawBitmap(56, 35, bmp_happy, 16, 16, COL_WARN);

    tft.setTextSize(2);
    tft.setTextColor(COL_WARN);
    tft.setCursor(10, 60);
    tft.print("Uploading");

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(10, 85);
    tft.print("Sending to GitHub...");
    tft.setCursor(10, 100);
    tft.print("Don't pull the plug!");

    // Wave animation at bottom
    for (int x = 0; x < 128; x += 16) {
        tft.drawBitmap(x, 140, bmp_wave, 16, 16, BLUE);
    }
}

void drawAdminMenuScreen() {
    tft.fillScreen(COL_BG);
    drawHeader("ADMIN MENU");

    tft.drawBitmap(56, 20, bmp_trophy, 16, 16, COL_WARN);

    const char* items[] = {"Calibration", "User Weights"};
    for (int i = 0; i < ADMIN_MENU_COUNT; i++) {
        int y = 44 + i * 22;
        if (i == adminMenuItem) {
            tft.fillRect(2, y - 2, TFT_WIDTH - 4, 16, COL_HEADER_BG);
            tft.setTextColor(COL_ACCENT);
            tft.setCursor(6, y); tft.print("> ");
        } else {
            tft.setTextColor(COL_LABEL);
            tft.setCursor(6, y); tft.print("  ");
        }
        tft.setTextSize(1);
        tft.print(items[i]);
    }
    drawKeyHints("UP", "DN", "EXIT", "OK");
}

void drawAdminWeightsScreen() {
    tft.fillScreen(COL_BG);
    drawHeader("USER WEIGHTS");

    int total = USER_COUNT + 1;
    int visEnd = min(adminEditUser / USER_VISIBLE * USER_VISIBLE + USER_VISIBLE, total);
    int visStart = visEnd - USER_VISIBLE;
    if (visStart < 0) visStart = 0;

    for (int i = visStart; i < min(visStart + USER_VISIBLE, total); i++) {
        int row = i - visStart;
        int y = 26 + row * 26;
        bool isGuest = (i == USER_COUNT);
        const char* name = isGuest ? "Guest" : userNames[i];

        if (i == adminEditUser) {
            tft.fillRect(2, y - 2, TFT_WIDTH - 4, 24, COL_HEADER_BG);
            tft.setTextColor(COL_ACCENT);
        } else {
            tft.setTextColor(COL_LABEL);
        }
        tft.setTextSize(1);
        tft.setCursor(6, y);
        tft.print(name);
        tft.setTextSize(2);
        tft.setTextColor(i == adminEditUser ? COL_VALUE : COL_DIVIDER);
        tft.setCursor(TFT_WIDTH - 48, y);
        tft.printf("%.0f", userWeights[i]);
        tft.setTextSize(1);
        tft.setTextColor(COL_LABEL);
        tft.print("kg");
    }
    drawKeyHints("UP", "DN", "BACK", "EDIT");
}

void drawAdminWeightEditScreen() {
    tft.fillScreen(COL_BG);
    bool isGuest = (adminEditUser == USER_COUNT);
    const char* name = isGuest ? "Guest" : userNames[adminEditUser];

    drawHeader("EDIT WEIGHT");

    tft.setTextSize(1);
    tft.setTextColor(COL_ACCENT);
    tft.setCursor(4, 24);
    tft.print(name);

    tft.setTextSize(3);
    tft.setTextColor(COL_VALUE);
    tft.setCursor(20, 50);
    tft.printf("%.0f", userWeights[adminEditUser]);
    tft.setTextSize(2);
    tft.print(" kg");

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 100);
    tft.print("\x18 +1 kg    \x19 -1 kg");

    drawKeyHints("+1", "-1", "BACK", "SAVE");
}

void drawCalibAuthScreen() {
    tft.fillScreen(COL_BG);
    drawHeader("ADMIN ACCESS");

    tft.drawBitmap(56, 22, bmp_skull, 16, 16, COL_WARN);

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 44);
    tft.print("Enter password:");

    // Show entered dots
    for (int i = 0; i < CALIB_PASSWORD_LEN; i++) {
        tft.setTextSize(2);
        tft.setTextColor(i < (int)pwdPos ? COL_ACCENT : COL_DIVIDER);
        tft.setCursor(16 + i * 26, 58);
        tft.print(i < (int)pwdPos ? "*" : "-");
    }

    if (pwdWrong) {
        tft.setTextSize(1);
        tft.setTextColor(COL_ERROR);
        tft.setCursor(4, 90);
        tft.print("WRONG PASSWORD!");
    }

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 108);
    tft.print("\x18=0  \x19=1  #=2  *=3");

    drawKeyHints("0", "1", "2", "3");
}

void drawCalibScreen() {
    tft.fillScreen(COL_BG);
    drawHeader("CALIBRATION");

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 22);
    tft.print("METERS / PULSE");

    tft.setTextSize(1);
    tft.setTextColor(COL_VALUE);
    tft.setCursor(4, 36);
    tft.printf("%.8f", calMetersPerPulse);

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 54);
    tft.printf("= %.4f m/rev", calMetersPerPulse * PULSES_PER_REV);

    drawDivider(68);

    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 74);
    tft.print("\x18 +0.000001");
    tft.setCursor(4, 88);
    tft.print("\x19 -0.000001");
    tft.setCursor(4, 102);
    tft.print("* SAVE & EXIT");
    tft.setCursor(4, 116);
    tft.print("# CANCEL");

    tft.setTextSize(1);
    tft.setTextColor(COL_WARN);
    tft.setCursor(4, 132);
    tft.printf("Default: %.7f", (float)METERS_PER_PULSE);

    drawKeyHints("+", "-", "BACK", "SAVE");
}

void drawUserSelectScreen() {
    tft.fillScreen(COL_BG);
    drawHeader("WHO ARE YOU?");

    // scroll up indicator
    if (userScrollOffset > 0) {
        tft.setTextColor(COL_LABEL);
        tft.setTextSize(1);
        tft.setCursor(60, 20);
        tft.print("\x1e");  // ▲
    }

    int totalUsers = USER_COUNT + 1;  // +1 for Guest
    int visibleEnd = min(userScrollOffset + USER_VISIBLE, totalUsers);
    for (int i = userScrollOffset; i < visibleEnd; i++) {
        int row = i - userScrollOffset;
        int y = 28 + row * 18;
        bool isGuest = (i == USER_COUNT);
        const char* name = isGuest ? "Guest" : userNames[i];

        if (i == currentUser) {
            tft.fillRect(2, y - 2, TFT_WIDTH - 4, 16, COL_HEADER_BG);
            tft.setTextColor(isGuest ? COL_WARN : COL_ACCENT);
            tft.setCursor(6, y);
            tft.print("> ");
        } else {
            tft.setTextColor(isGuest ? COL_WARN : COL_LABEL);
            tft.setCursor(6, y);
            tft.print("  ");
        }
        tft.setTextSize(1);
        tft.print(name);

        // counter on right (Guest shows no number)
        if (!isGuest) {
            tft.setTextColor(COL_DIVIDER);
            tft.setCursor(TFT_WIDTH - 20, y);
            tft.printf("%d/%d", i + 1, USER_COUNT);
        }
    }

    // scroll down indicator
    if (userScrollOffset + USER_VISIBLE < USER_COUNT + 1) {
        tft.setTextColor(COL_LABEL);
        tft.setTextSize(1);
        tft.setCursor(60, 140);
        tft.print("\x1f");  // ▼
    }

    drawKeyHints("UP", "DN", "BACK", "GO");
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
    lastPulseIntervalMs = 0;
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

        // Smooth the single-pulse interval (raw value is very noisy)
        if (lastPulseIntervalMs > 0) {
            float iv = (float)lastPulseIntervalMs;
            emaIntervalMs = (emaIntervalMs > 0) ? (0.6f * emaIntervalMs + 0.4f * iv) : iv;

            // Boat speed from smoothed angular velocity (omega ~ 1/interval), then smooth again
            float spd = calMetersPerPulse / (emaIntervalMs / 1000.0f);
            emaSpeed = (emaSpeed > 0) ? (0.7f * emaSpeed + 0.3f * spd) : spd;
            currentSpeed = emaSpeed;
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
                // drive: track the min interval; a rise past hysteresis = recovery start
                if (emaIntervalMs < extremaInterval || extremaInterval == 0) extremaInterval = emaIntervalMs;
                if (emaIntervalMs > extremaInterval * (1.0f + STROKE_HYST)) {
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
    }
}

// ───────── Handle input ─────────
void handleInput() {
    switch (currentScreen) {
    case SCR_IDLE:
        if (consume(btnUp))   { pushCombo(0); brightness = min(255, brightness + 25); setBacklight(brightness); }
        if (consume(btnDown)) { pushCombo(1); brightness = max((uint8_t)10, (uint8_t)(brightness - 25)); setBacklight(brightness); }
        if (consume(btnHash)) { pushCombo(2); beep(); displayPage = 0; currentScreen = SCR_HISTORY; }
        if (consume(btnStar)) {
            pushCombo(3);
            if (checkCombo()) {
                // Secret combo entered → go to admin auth
                memset(comboBuffer, 0, sizeof(comboBuffer));
                comboPos = 0;
                pwdPos = 0; pwdWrong = false;
                beep(); delay(80); beep();
#if CALIB_REQUIRE_PASSWORD
                currentScreen = SCR_CALIB_AUTH;
#else
                adminMenuItem = 0;
                currentScreen = SCR_ADMIN_MENU;   // no password: combo opens admin directly
#endif
            } else {
                beep();
                currentUser = 0;
                userScrollOffset = 0;
                currentScreen = SCR_USER_SELECT;
            }
        }
        break;

    case SCR_CALIB_AUTH:
        { // password entry — each button appends a digit
            uint8_t pressed = 0xFF;
            if (consume(btnUp))   pressed = 0;
            if (consume(btnDown)) pressed = 1;
            if (consume(btnHash)) pressed = 2;
            if (consume(btnStar)) pressed = 3;
            if (pressed != 0xFF) {
                beep();
                pwdWrong = false;
                pwdBuffer[pwdPos++] = pressed;
                if (pwdPos >= CALIB_PASSWORD_LEN) {
                    bool ok = true;
                    for (int i = 0; i < CALIB_PASSWORD_LEN; i++)
                        if (pwdBuffer[i] != calibPassword[i]) { ok = false; break; }
                    if (ok) {
                        playTone(1600, 100);
                        adminMenuItem = 0;
                        currentScreen = SCR_ADMIN_MENU;
                    } else {
                        pwdWrong = true;
                        pwdPos = 0;
                        playTone(200, 300);
                    }
                }
            }
        }
        break;

    case SCR_ADMIN_MENU:
        if (consume(btnUp))   { if (adminMenuItem > 0) adminMenuItem--; }
        if (consume(btnDown)) { if (adminMenuItem < ADMIN_MENU_COUNT - 1) adminMenuItem++; }
        if (consume(btnHash)) { beep(); currentScreen = SCR_IDLE; }
        if (consume(btnStar)) {
            beep();
            if (adminMenuItem == 0) currentScreen = SCR_CALIB;
            else if (adminMenuItem == 1) { adminEditUser = 0; currentScreen = SCR_ADMIN_WEIGHTS; }
        }
        break;

    case SCR_CALIB:
        if (consume(btnUp))   { calMetersPerPulse += 0.000001f; }
        if (consume(btnDown)) { if (calMetersPerPulse > 0.000001f) calMetersPerPulse -= 0.000001f; }
        if (consume(btnStar)) {
            saveCalibration();
            playTone(TONE_UPLOAD, 200);
            currentScreen = SCR_ADMIN_MENU;
        }
        if (consume(btnHash)) {
            loadCalibration();
            beep();
            currentScreen = SCR_ADMIN_MENU;
        }
        break;

    case SCR_ADMIN_WEIGHTS:
        if (consume(btnUp))   { if (adminEditUser > 0) adminEditUser--; }
        if (consume(btnDown)) { if (adminEditUser < USER_COUNT) adminEditUser++; }
        if (consume(btnHash)) { beep(); currentScreen = SCR_ADMIN_MENU; }
        if (consume(btnStar)) { beep(); currentScreen = SCR_ADMIN_WEIGHT_EDIT; }
        break;

    case SCR_ADMIN_WEIGHT_EDIT:
        if (consume(btnUp))   { userWeights[adminEditUser] = min(200.0f, userWeights[adminEditUser] + 1.0f); }
        if (consume(btnDown)) { userWeights[adminEditUser] = max(20.0f,  userWeights[adminEditUser] - 1.0f); }
        if (consume(btnStar)) {
            saveWeights();
            playTone(TONE_UPLOAD, 200);
            currentScreen = SCR_ADMIN_WEIGHTS;
        }
        if (consume(btnHash)) {
            loadCalibration();  // revert
            beep();
            currentScreen = SCR_ADMIN_WEIGHTS;
        }
        break;

    case SCR_USER_SELECT:
        if (consume(btnUp)) {
            if (currentUser > 0) {
                currentUser--;
                if (currentUser < userScrollOffset)
                    userScrollOffset = currentUser;
            }
        }
        if (consume(btnDown)) {
            if (currentUser < USER_COUNT) {  // USER_COUNT = Guest index
                currentUser++;
                if (currentUser >= userScrollOffset + USER_VISIBLE)
                    userScrollOffset = currentUser - USER_VISIBLE + 1;
            }
        }
        if (consume(btnHash)) { beep(); currentScreen = SCR_IDLE; }
        if (consume(btnStar)) {
            beep();
            resetWorkout();
            workoutActive = true;
            workoutStartMs = millis();
            currentScreen = SCR_WORKOUT;
            playTone(TONE_START, TONE_DURATION);
        }
        break;

    case SCR_WORKOUT:
        if (consume(btnHash)) {
            workoutElapsedMs += millis() - workoutStartMs;
            workoutActive = false;
            currentScreen = SCR_PAUSED;
            playTone(TONE_STOP, TONE_DURATION);
        }
        break;

    case SCR_PAUSED:
        if (consume(btnStar)) {
            beep();
            workoutActive = true;
            workoutStartMs = millis();
            currentScreen = SCR_WORKOUT;
            playTone(TONE_START, TONE_DURATION);
        }
        if (consume(btnHash)) {
            beep();
            currentScreen = SCR_UPLOADING;
        }
        break;

    case SCR_SUMMARY:
        if (consume(btnHash)) {
            beep();
            currentScreen = SCR_IDLE;
        }
        break;

    case SCR_HISTORY:
        if (consume(btnUp))   { if (displayPage > 0) displayPage--; }
        if (consume(btnDown)) { if (displayPage < historyCount - 1) displayPage++; }
        if (consume(btnHash)) { beep(); currentScreen = SCR_IDLE; }
        break;

    default: break;
    }
}

// ───────── Display refresh ─────────
unsigned long lastRedraw = 0;
#define REDRAW_MS 250
Screen lastDrawnScreen = (Screen)-1;

void refreshDisplay() {
    bool needsRedraw = (currentScreen != lastDrawnScreen);
    if (!needsRedraw && millis() - lastRedraw < REDRAW_MS) return;
    lastRedraw = millis();
    lastDrawnScreen = currentScreen;

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
        default: break;
    }
}

// ───────── Setup ─────────
void setup() {
    Serial.begin(115200);

    // Backlight (LEDC channel 0)
    if (TFT_BL >= 0) {
        ledcSetup(0, 5000, 8);
        ledcAttachPin(TFT_BL, 0);
        setBacklight(brightness);
    }

    // Speaker (LEDC channel 1)
    ledcSetup(1, 2000, 8);
    ledcAttachPin(SPEAKER_PIN, 1);

    // TFT
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(0);
    tft.fillScreen(COL_BG);
    tft.setTextWrap(false);

    // Splash with animation
    tft.fillScreen(COL_BG);

    // Animated waves filling up
    for (int y = 150; y >= 90; y -= 4) {
        for (int x = 0; x < 128; x += 16) {
            tft.drawBitmap(x, y, bmp_wave, 16, 16, BLUE);
        }
        delay(40);
    }

    // Rower appears
    tft.drawBitmap(56, 60, bmp_rower, 16, 16, COL_ACCENT);
    delay(200);

    // Title
    tft.setTextSize(2);
    tft.setTextColor(COL_ACCENT);
    tft.setCursor(8, 20);
    tft.print("WATER");
    tft.setCursor(8, 38);
    tft.print("ROWER");

    tft.setTextSize(1);
    tft.setTextColor(COL_VALUE);
    tft.setCursor(80, 25);
    tft.print("v2.0");

    tft.setTextColor(COL_LABEL);
    tft.setCursor(20, 80);
#ifdef SENSOR_TYPE_LDR
    tft.print("Sensor: LDR");
#elif defined(SENSOR_TYPE_HALL)
    tft.print("Sensor: Hall");
#elif defined(SENSOR_TYPE_BLOCKER)
    tft.print("Sensor: Blocker");
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

    // Button pins
    pinMode(BTN_UP_PIN,   INPUT_PULLUP);
    pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
    pinMode(BTN_HASH_PIN, INPUT_PULLUP);
    pinMode(BTN_STAR_PIN, INPUT_PULLUP);

    // Load saved calibration from NVS
    loadCalibration();

    // BLE FTMS
    initBLE();

    // WiFi + NTP
    connectWiFi();
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    // Startup sound
    playTone(800, 100);
    delay(50);
    playTone(1200, 100);
    delay(50);
    playTone(1600, 150);

    delay(1000);
    currentScreen = SCR_IDLE;
}

// ───────── Loop ─────────
void loop() {
    readAllButtons();
    handleInput();

    // Read sensor (LDR: fast polling / Hall: ISR updates globals)
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
            // Guest: screen + BLE only, no GitHub
            beep();
        } else {
            drawUploadingScreen();
            ok = uploadToGitHub(durSec);
            if (ok) {
                playTone(TONE_UPLOAD, 200);
            } else {
                playTone(300, 500);
            }
        }
        drawSummaryScreen(ok);
        currentScreen = SCR_SUMMARY;
        lastDrawnScreen = SCR_SUMMARY;
        return;
    }

    refreshDisplay();
}
