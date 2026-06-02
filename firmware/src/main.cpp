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
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_http_client.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "config.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_bt.h"

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
    "Burn those calories!",
    "Muscles miss you!",
    "Row or regret?",
    "One more session!",
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
uint16_t strokeGapMs    = STROKE_GAP_MS;
float userWeights[USER_COUNT + 1];   // +1 for Guest (index USER_COUNT)

void loadCalibration() {
    prefs.begin("wrower", true);
    calMetersPerPulse = prefs.getFloat("mpp", METERS_PER_PULSE);
    strokeGapMs       = prefs.getUShort("sgap", STROKE_GAP_MS);
    for (int i = 0; i <= USER_COUNT; i++) {
        char key[6]; snprintf(key, sizeof(key), "w%d", i);
        userWeights[i] = prefs.getFloat(key, DEFAULT_WEIGHT_KG);
    }
    prefs.end();
}
void saveCalibration() {
    prefs.begin("wrower", false);
    prefs.putFloat("mpp", calMetersPerPulse);
    prefs.putUShort("sgap", strokeGapMs);
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
uint8_t pwdPos        = 0;
bool    pwdWrong      = false;
uint8_t pwdWrongCount = 0;
unsigned long pwdLockUntilMs = 0;
char pwdAttemptTimes[3][32] = {};
uint8_t pwdAttemptKeys[3][CALIB_PASSWORD_LEN] = {};

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

// ───────── Per-user auth ─────────
uint8_t userPasswords[USER_COUNT][USER_PASSWORD_LEN];

void loadPasswords() {
    static const uint8_t defaults[USER_COUNT][USER_PASSWORD_LEN] = USER_PASSWORDS;
    prefs.begin("wrower", true);
    for (int i = 0; i < USER_COUNT; i++) {
        char key[5]; snprintf(key, sizeof(key), "pw%d", i);
        if (prefs.getBytesLength(key) == USER_PASSWORD_LEN)
            prefs.getBytes(key, userPasswords[i], USER_PASSWORD_LEN);
        else
            memcpy(userPasswords[i], defaults[i], USER_PASSWORD_LEN);
    }
    prefs.end();
}
void savePassword(int userIdx) {
    prefs.begin("wrower", false);
    char key[5]; snprintf(key, sizeof(key), "pw%d", userIdx);
    prefs.putBytes(key, userPasswords[userIdx], USER_PASSWORD_LEN);
    prefs.end();
}
// Returns true if this user has no password (all bytes == 0xFF)
bool userNoPassword(int u) {
    for (int i = 0; i < USER_PASSWORD_LEN; i++)
        if (userPasswords[u][i] != 0xFF) return false;
    return true;
}

uint8_t  userPwdPos        = 0;
bool     userPwdWrong      = false;
uint8_t  userPwdBuffer[USER_PASSWORD_LEN] = {};
uint8_t  userWrongCount[USER_COUNT]       = {};
unsigned long userLockUntilMs[USER_COUNT] = {};
char     userAttemptTimes[USER_COUNT][3][32] = {};
uint8_t  userAttemptKeys[USER_COUNT][3][USER_PASSWORD_LEN] = {};

String getTimestamp();  // forward declaration

// ───────── Suspicious log (last 5 lockout events) ─────────
#define SUSPICIOUS_MAX 5
struct SuspiciousEntry {
    char name[16];
    char time[32];
};
SuspiciousEntry suspiciousLog[SUSPICIOUS_MAX];
int suspiciousCount = 0;
int suspiciousHead  = 0;

void addSuspicious(const char* name) {
    int idx = suspiciousHead % SUSPICIOUS_MAX;
    strncpy(suspiciousLog[idx].name, name, sizeof(suspiciousLog[idx].name) - 1);
    suspiciousLog[idx].name[sizeof(suspiciousLog[idx].name) - 1] = '\0';
    String ts = getTimestamp();
    strncpy(suspiciousLog[idx].time, ts.c_str(), sizeof(suspiciousLog[idx].time) - 1);
    suspiciousLog[idx].time[sizeof(suspiciousLog[idx].time) - 1] = '\0';
    suspiciousHead++;
    if (suspiciousCount < SUSPICIOUS_MAX) suspiciousCount++;
}

void appendSuspiciousJson(JsonDocument& doc) {
    JsonArray arr = doc.createNestedArray("suspicious_history");
    int total = suspiciousCount < SUSPICIOUS_MAX ? suspiciousCount : SUSPICIOUS_MAX;
    // oldest → newest order
    for (int i = 0; i < total; i++) {
        int idx = (suspiciousHead - total + i + SUSPICIOUS_MAX * 2) % SUSPICIOUS_MAX;
        JsonObject e = arr.createNestedObject();
        e["user"] = suspiciousLog[idx].name;
        e["time"] = suspiciousLog[idx].time;
    }
}

// ───────── Admin state ─────────
int adminMenuItem   = 0;
int adminEditUser   = 0;
int adminPwdUser    = 0;
uint8_t newPwdBuffer[USER_PASSWORD_LEN] = {};
uint8_t newPwdPos   = 0;
#define ADMIN_MENU_COUNT 4
uint32_t calibPulseStart = 0;   // pulse snapshot on entering calib screen
static const uint16_t calibRefOptions[] = {10, 20, 50, 100, 200, 500, 1000};
static const int calibRefCount = 7;
int calibRefIdx = 3;            // default 100m

// ───────── State machine ─────────
enum Screen { SCR_IDLE, SCR_USER_SELECT, SCR_USER_AUTH, SCR_GUEST_WEIGHT, SCR_WORKOUT, SCR_PAUSED, SCR_SUMMARY,
              SCR_UPLOADING, SCR_HISTORY,
              SCR_CALIB_AUTH, SCR_ADMIN_MENU, SCR_CALIB,
              SCR_ADMIN_WEIGHTS, SCR_ADMIN_WEIGHT_EDIT,
              SCR_ADMIN_PASSWORDS, SCR_ADMIN_PASSWORD_EDIT,
              SCR_ADMIN_STROKE_GAP };
Screen currentScreen = SCR_IDLE;
int displayPage = 0;

// ───────── Sensor State ─────────
uint32_t pulseCount     = 0;
unsigned long lastPulseMs = 0;
unsigned long lastPulseIntervalMs = 0;

#ifdef SENSOR_TYPE_LDR
bool     ldrPulseActive = false;
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
bool     workoutActive  = false;
bool     lastUploadOk   = false;
uint32_t totalPulses    = 0;
float    totalMeters    = 0;
float    totalCalories  = 0;
uint32_t strokeCount    = 0;
float    currentSpeed   = 0;
float    strokeRate     = 0;
unsigned long workoutStartMs   = 0;
unsigned long workoutElapsedMs = 0;
unsigned long lastStrokeBoundary  = 0;
bool          inStroke            = false;
float         smoothedIntervalMs  = 0;   // fast EMA for display / drive detection
float         bgIntervalMs        = 0;   // slow EMA background
bool          inDrive             = false;

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

void loadHistory() {
    prefs.begin("wrower", true);
    historyCount = prefs.getInt("hcnt", 0);
    if (historyCount > MAX_HISTORY) historyCount = MAX_HISTORY;
    for (int i = 0; i < historyCount; i++) {
        char key[6]; snprintf(key, sizeof(key), "h%d", i);
        prefs.getBytes(key, &history[i], sizeof(WorkoutRecord));
    }
    prefs.end();
}
void persistHistory() {
    prefs.begin("wrower", false);
    // If NVS is almost full (<15 free entries), drop oldest records until safe
    while (historyCount > 1 && prefs.freeEntries() < 15) {
        historyCount--;
        Serial.printf("[NVS] almost full, trimmed to %d records\n", historyCount);
    }
    prefs.putInt("hcnt", historyCount);
    for (int i = 0; i < historyCount; i++) {
        char key[6]; snprintf(key, sizeof(key), "h%d", i);
        prefs.putBytes(key, &history[i], sizeof(WorkoutRecord));
    }
    prefs.end();
}

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
void readSensor() {
    if (!workoutActive && currentScreen != SCR_CALIB) return;

    int val = analogRead(LDR_SIGNAL_PIN);

    // Detect falling edge: light blocked → paddle passing
    if (!ldrPulseActive && val < (LDR_THRESHOLD - LDR_HYSTERESIS)) {
        ldrPulseActive = true;
        unsigned long now = millis();
        if (lastPulseMs > 0) {
            lastPulseIntervalMs = now - lastPulseMs;
        }
        lastPulseMs = now;
        pulseCount++;
    }
    // Detect rising edge: light restored → paddle passed
    else if (ldrPulseActive && val > (LDR_THRESHOLD + LDR_HYSTERESIS)) {
        ldrPulseActive = false;
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
    if (!workoutActive && currentScreen != SCR_CALIB) return;

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
    if (!workoutActive && currentScreen != SCR_CALIB) return;

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
    // Max TX power for best range
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV,     ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);

    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new FTMSCallbacks());

    // FTMS Service
    BLEService* ftmsService = bleServer->createService(FTMS_SERVICE_UUID);

    // FTMS Feature (required) — declare rower support
    ftmsFeatureChar = ftmsService->createCharacteristic(
        FTMS_FEATURE_UUID, BLECharacteristic::PROPERTY_READ);
    uint8_t ftmsFeatures[8] = {0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ftmsFeatureChar->setValue(ftmsFeatures, 8);

    // Rower Data (notify)
    rowerDataChar = ftmsService->createCharacteristic(
        ROWER_DATA_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    rowerDataChar->addDescriptor(new BLE2902());

    ftmsService->start();

    // Advertise — include FTMS UUID so fitness apps discover the device
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(FTMS_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);   // connection interval hint (helps iOS)
    adv->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();
}

void updateBLE() {
    if (!bleClientConnected) return;
    if (!workoutActive && currentScreen != SCR_CALIB) return;
    if (millis() - lastBleUpdate < BLE_UPDATE_INTERVAL_MS) return;
    lastBleUpdate = millis();

    // FTMS Rower Data format (per Bluetooth spec)
    // Flags (2 bytes) + fields
    unsigned long elapsed = workoutElapsedMs + (millis() - workoutStartMs);
    uint16_t strokeRateX2 = (uint16_t)(strokeRate * 2);  // 0.5 resolution
    uint16_t totalDist = (uint16_t)totalMeters;           // meters (wraps at 65535)
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
    data[idx++] = 0; // high byte
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
    // Wait for DHCP: IP must be assigned and non-zero
    for (int i = 0; i < 20 && WiFi.localIP() == IPAddress(0,0,0,0); i++)
        delay(200);
    delay(500);  // let routing table settle
    Serial.printf("[WIFI] IP=%s GW=%s\n",
        WiFi.localIP().toString().c_str(),
        WiFi.gatewayIP().toString().c_str());
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

// (GitHub upload removed — using TrueNAS NAS instead)
// USERTrust ECC root cert kept in case GitHub upload is re-enabled later
static const char* GITHUB_ROOT_CA =
"-----BEGIN CERTIFICATE-----\n"
"MIICjzCCAhWgAwIBAgIQXIuZxVqUxdJxVt7NiYDMJjAKBggqhkjOPQQDAzCBiDEL\n"
"MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl\n"
"eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT\n"
"JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMjAx\n"
"MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgT\n"
"Ck5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVUaGUg\n"
"VVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBFQ0MgQ2VydGlm\n"
"aWNhdGlvbiBBdXRob3JpdHkwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQarFRaqflo\n"
"I+d61SRvU8Za2EurxtW20eZzca7dnNYMYf3boIkDuAUU7FfO7l0/4iGzzvfUinng\n"
"o4N+LZfQYcTxmdwlkWOrfzCjtHDix6EznPO/LlxTsV+zfTJ/ijTjeXmjQjBAMB0G\n"
"A1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAOBgNVHQ8BAf8EBAMCAQYwDwYD\n"
"VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNoADBlAjA2Z6EWCNzklwBBHU6+4WMB\n"
"zzuqQhFkoJ2UOQIReVx7Hfpkue4WQrO/isIJxOzksU0CMQDpKmFHjFJKS04YcPbW\n"
"RNZu9YO6bVi9JNlWSOrvxKJGgYhqOkbRqZtNyWHa0V1Xahg=\n"
"-----END CERTIFICATE-----\n";

// ───────── TrueNAS Upload (plain HTTP, local network) ─────────
bool uploadToGitHub(uint32_t durSec) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return false;

    // Build workout JSON
    static StaticJsonDocument<1024> doc;
    doc.clear();
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

    // Build file path on NAS
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

    // Multipart form body for TrueNAS filesystem/put API
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

    // Plain HTTP POST — no TLS, no BIGNUM issues
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

// ───────── Bad Attempt Upload ─────────
void uploadBadAttempt() {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;

    static StaticJsonDocument<1024> doc;
    doc.clear();
    doc["machine_sn"]    = MACHINE_SN;
    doc["machine_model"] = MACHINE_MODEL;
    doc["event"]         = "bad_admin_attempt";
    doc["attempts"]      = 3;
    doc["timestamp"]     = getTimestamp();
    JsonObject times = doc.createNestedObject("attempt_times");
    for (int i = 0; i < 3; i++) {
        char key[2] = {'1' + i, '\0'};
        JsonObject entry = times.createNestedObject(key);
        entry["time"] = pwdAttemptTimes[i];
        char entered[CALIB_PASSWORD_LEN + 1];
        for (int j = 0; j < CALIB_PASSWORD_LEN; j++)
            entered[j] = '0' + pwdAttemptKeys[i][j];
        entered[CALIB_PASSWORD_LEN] = '\0';
        entry["entered"] = entered;
    }
    appendSuspiciousJson(doc);

    String content;
    serializeJsonPretty(doc, content);

    size_t encodedLen = 0;
    mbedtls_base64_encode(NULL, 0, &encodedLen,
        (const unsigned char*)content.c_str(), content.length());
    char* encoded = (char*)malloc(encodedLen + 1);
    if (!encoded) return;
    mbedtls_base64_encode((unsigned char*)encoded, encodedLen + 1, &encodedLen,
        (const unsigned char*)content.c_str(), content.length());
    encoded[encodedLen] = '\0';

    struct tm _ti;
    String filename;
    if (getLocalTime(&_ti, 100)) {
        char _pb[56];
        snprintf(_pb, sizeof(_pb), "bad_attempt/%02d/%02d/%02d/%02d%02d%02d.json",
            _ti.tm_mday, _ti.tm_mon + 1, _ti.tm_year % 100,
            _ti.tm_hour, _ti.tm_min, _ti.tm_sec);
        filename = String(_pb);
    } else {
        filename = "bad_attempt/" + String(MACHINE_SN) + "_unknown.json";
    }

    String url = "https://api.github.com/repos/";
    url += GITHUB_OWNER; url += "/";
    url += GITHUB_REPO;  url += "/contents/"; url += filename;

    static StaticJsonDocument<1024> apiDoc;
    apiDoc.clear();
    apiDoc["message"] = "Security: 3 bad admin attempts " + getTimestamp();
    apiDoc["content"] = encoded;
    apiDoc["branch"]  = GITHUB_BRANCH;
    String apiBody;
    serializeJson(apiDoc, apiBody);
    free(encoded);

    WiFiClientSecure client;
    client.setCACert(GITHUB_ROOT_CA);
    HTTPClient http;
    http.begin(client, url);
    http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("Content-Type", "application/json");
    http.PUT(apiBody);
    http.end();
}

// ───────── User Bad Attempt Upload ─────────
void uploadUserBadAttempt(int userIdx) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;

    static StaticJsonDocument<1024> doc;
    doc.clear();
    doc["machine_sn"]    = MACHINE_SN;
    doc["machine_model"] = MACHINE_MODEL;
    doc["event"]         = "bad_user_attempt";
    doc["user"]          = userNames[userIdx];
    doc["attempts"]      = 3;
    doc["timestamp"]     = getTimestamp();
    JsonObject times = doc.createNestedObject("attempt_times");
    for (int i = 0; i < 3; i++) {
        char key[2] = {'1' + i, '\0'};
        JsonObject entry = times.createNestedObject(key);
        entry["time"] = userAttemptTimes[userIdx][i];
        char entered[USER_PASSWORD_LEN + 1];
        for (int j = 0; j < USER_PASSWORD_LEN; j++)
            entered[j] = '0' + userAttemptKeys[userIdx][i][j];
        entered[USER_PASSWORD_LEN] = '\0';
        entry["entered"] = entered;
    }
    appendSuspiciousJson(doc);

    String content;
    serializeJsonPretty(doc, content);

    size_t encodedLen = 0;
    mbedtls_base64_encode(NULL, 0, &encodedLen,
        (const unsigned char*)content.c_str(), content.length());
    char* encoded = (char*)malloc(encodedLen + 1);
    if (!encoded) return;
    mbedtls_base64_encode((unsigned char*)encoded, encodedLen + 1, &encodedLen,
        (const unsigned char*)content.c_str(), content.length());
    encoded[encodedLen] = '\0';

    struct tm _ti;
    String filename;
    if (getLocalTime(&_ti, 100)) {
        char _pb[64];
        snprintf(_pb, sizeof(_pb), "bad_attempt/%02d/%02d/%02d/%s_%02d%02d%02d.json",
            _ti.tm_mday, _ti.tm_mon + 1, _ti.tm_year % 100,
            userNames[userIdx],
            _ti.tm_hour, _ti.tm_min, _ti.tm_sec);
        filename = String(_pb);
    } else {
        filename = "bad_attempt/" + String(userNames[userIdx]) + "_unknown.json";
    }

    String url = "https://api.github.com/repos/";
    url += GITHUB_OWNER; url += "/";
    url += GITHUB_REPO;  url += "/contents/"; url += filename;

    static StaticJsonDocument<1024> apiDoc;
    apiDoc.clear();
    apiDoc["message"] = "Security: 3 bad attempts for user " + String(userNames[userIdx]) + " " + getTimestamp();
    apiDoc["content"] = encoded;
    apiDoc["branch"]  = GITHUB_BRANCH;
    String apiBody;
    serializeJson(apiDoc, apiBody);
    free(encoded);

    WiFiClientSecure client;
    client.setCACert(GITHUB_ROOT_CA);
    HTTPClient http;
    http.begin(client, url);
    http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("Content-Type", "application/json");
    http.PUT(apiBody);
    http.end();
}

// ───────── Drawing Helpers ─────────
void drawHeader(const char* title) {
    tft.fillRect(0, 0, TFT_WIDTH, 18, COL_HEADER_BG);
    tft.setTextSize(1);
    tft.setTextColor(COL_ACCENT, COL_HEADER_BG);
    tft.setCursor(4, 5);
    tft.print(title);
    // BLE indicator
    tft.setCursor(TFT_WIDTH - 36, 5);
    tft.setTextColor(COL_VALUE, COL_HEADER_BG);
    tft.print(bleClientConnected ? "B+" : "b+");
    // WiFi indicator
    tft.setCursor(TFT_WIDTH - 16, 5);
    tft.setTextColor(WiFi.status() == WL_CONNECTED ? COL_VALUE : COL_ERROR, COL_HEADER_BG);
    tft.print(WiFi.status() == WL_CONNECTED ? "W+" : "W-");
}

void drawDivider(int y) {
    tft.drawFastHLine(4, y, TFT_WIDTH - 8, COL_DIVIDER);
}

void drawLabelValue(int x, int y, const char* label, const char* value, uint16_t valColor = COL_VALUE) {
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(x, y);
    tft.print(label);
    tft.setTextColor(valColor, COL_BG);
    tft.setCursor(x, y + 12);
    tft.setTextSize(2);
    tft.print(value);
}

void drawKeyHints(const char* up, const char* down, const char* hash, const char* star) {
    int y = TFT_HEIGHT - 11;
    tft.fillRect(0, y - 2, TFT_WIDTH, 13, COL_HEADER_BG);
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_HEADER_BG);
    if (up && up[0])   { tft.setCursor(2, y);   tft.printf("^%s", up); }
    if (down && down[0]){ tft.setCursor(36, y);  tft.printf("v%s", down); }
    if (hash && hash[0]){ tft.setCursor(68, y);  tft.printf("#%s", hash); }
    if (star && star[0]){ tft.setCursor(100, y); tft.printf("*%s", star); }
}

// ───────── Screens ─────────
void drawIdleScreen() {
    // screen cleared by refreshDisplay() on transition
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
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(30, 22);
    tft.print(MACHINE_SN);

    drawDivider(48);

    // Sensor value — changes every redraw, clear line first
    tft.fillRect(0, 51, TFT_WIDTH, 9, COL_BG);
    int sensorVal = getSensorRaw();
    tft.setCursor(4, 52);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setTextSize(1);
    tft.printf("%s: %d", getSensorLabel(), sensorVal);

    tft.setCursor(4, 66);
    tft.setTextColor(COL_ACCENT, COL_BG);
    tft.setTextSize(2);
    tft.print("READY");

    // Idle message — changes every 3s, clear full line
    tft.fillRect(0, 87, TFT_WIDTH, 9, COL_BG);
    tft.setTextSize(1);
    tft.setTextColor(COL_WARN, COL_BG);
    tft.setCursor(4, 88);
    tft.print(idle_msgs[millis() / 3000 % NUM_IDLE_MSGS]);

    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(4, 108);
    tft.print("* START  # HISTORY");

    drawKeyHints("BRT+", "BRT-", "HIST", "GO");
}

void drawWorkoutScreen() {
    // screen cleared by refreshDisplay() on transition
    drawHeader("ROWING");

    unsigned long elapsed = workoutElapsedMs + (millis() - workoutStartMs);
    uint32_t sec = elapsed / 1000;
    uint32_t mn = sec / 60;
    uint32_t s  = sec % 60;

    // Animated rower icon — erase old position before drawing new
    tft.fillRect(4, 21, 24, 16, COL_BG);
    int rowerX = 4 + (millis() / 400 % 3) * 2;
    tft.drawBitmap(rowerX, 21, bmp_rower, 16, 16, COL_ACCENT);

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mn, s);
    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setCursor(30, 24);
    tft.print(timeBuf);

    drawDivider(42);

    // Distance — always clear fire area, draw fire only when fast
    char distBuf[16];
    snprintf(distBuf, sizeof(distBuf), "%5.0f", totalMeters);
    drawLabelValue(4, 46, "DISTANCE (m)", distBuf);
    tft.fillRect(108, 46, 16, 16, COL_BG);
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

    // Motivational message — clear full line before printing (variable length)
    drawDivider(103);
    tft.fillRect(0, 105, TFT_WIDTH, 9, COL_BG);
    tft.setTextSize(1);
    tft.setTextColor(COL_WARN, COL_BG);
    tft.setCursor(4, 106);
    tft.print(fun_msgs[(sec / 10) % NUM_FUN_MSGS]);

    // SPM & CAL
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(4, 118);  tft.print("SPM");
    tft.setCursor(68, 118); tft.print("CAL");
    tft.setTextSize(2);
    tft.setTextColor(COL_VALUE, COL_BG);
    tft.setCursor(4, 130);  tft.printf("%-3.0f", strokeRate);
    tft.setCursor(68, 130); tft.printf("%.0f", totalCalories);

    // Animated water — erase row then redraw at new offset
    tft.fillRect(0, 148, TFT_WIDTH, 12, COL_BG);
    int waveOffset = (millis() / 300) % 16;
    for (int x = waveOffset - 16; x < 128; x += 16) {
        tft.drawBitmap(x, 148, bmp_wave, 16, 16, BLUE);
    }

    drawKeyHints("", "", "STOP", "");
}

void drawPausedScreen() {
    // screen cleared by refreshDisplay() on transition
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
    tft.setCursor(4, 84);   tft.printf("Dist: %.0f m", totalMeters);
    tft.setCursor(4, 98);   tft.printf("Strokes: %d", strokeCount);
    tft.setCursor(4, 112);  tft.printf("Cal: %.0f", totalCalories);

    // Motivational nudge
    tft.setTextColor(COL_ACCENT);
    tft.setCursor(4, 130);
    tft.print("* = Back in there!");

    drawKeyHints("", "", "SAVE", "GO!");
}

void drawSummaryScreen(bool uploadOk) {
    // screen cleared by refreshDisplay() on transition
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

    drawKeyHints("", "", "BACK", (!uploadOk && currentUser != USER_COUNT) ? "RETRY" : "");
}

void drawHistoryScreen() {
    // screen cleared by refreshDisplay() on transition
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
    // screen cleared by refreshDisplay() on transition
    drawHeader("SAVING...");

    // Animated dots loading effect
    tft.drawBitmap(56, 35, bmp_happy, 16, 16, COL_WARN);

    tft.setTextSize(2);
    tft.setTextColor(COL_WARN);
    tft.setCursor(10, 60);
    tft.print("Uploading");

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 85);
    tft.print("Sending to GitHub...");
    tft.setCursor(4, 100);
    tft.print("Don't pull the plug!");

    // Wave animation at bottom
    for (int x = 0; x < 128; x += 16) {
        tft.drawBitmap(x, 140, bmp_wave, 16, 16, BLUE);
    }
}

void drawAdminMenuScreen() {
    // screen cleared by refreshDisplay() on transition
    drawHeader("ADMIN MENU");

    tft.drawBitmap(56, 20, bmp_trophy, 16, 16, COL_WARN);

    const char* items[] = {"Calibration", "User Weights", "Passwords", "Stroke Gap"};
    for (int i = 0; i < ADMIN_MENU_COUNT; i++) {
        int y = 44 + i * 22;
        if (i == adminMenuItem) {
            tft.fillRect(2, y - 2, TFT_WIDTH - 4, 16, COL_HEADER_BG);
            tft.setTextColor(COL_ACCENT, COL_HEADER_BG);
            tft.setCursor(6, y); tft.print("> ");
        } else {
            tft.fillRect(2, y - 2, TFT_WIDTH - 4, 16, COL_BG);
            tft.setTextColor(COL_LABEL, COL_BG);
            tft.setCursor(6, y); tft.print("  ");
        }
        tft.setTextSize(1);
        tft.print(items[i]);
    }
    drawKeyHints("UP", "DN", "EXIT", "OK");
}

void drawAdminWeightsScreen() {
    // screen cleared by refreshDisplay() on transition
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
    // screen cleared by refreshDisplay() on transition
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
    tft.print("^ +1 kg    v -1 kg");

    drawKeyHints("+1", "-1", "BACK", "SAVE");
}

void drawAdminPasswordsScreen() {
    // screen cleared by refreshDisplay() on transition
    drawHeader("USER PASSWORDS");

    for (int i = 0; i < USER_COUNT; i++) {
        int y = 26 + i * 26;
        if (i == adminPwdUser) {
            tft.fillRect(2, y - 2, TFT_WIDTH - 4, 24, COL_HEADER_BG);
            tft.setTextColor(COL_ACCENT);
        } else {
            tft.setTextColor(COL_LABEL);
        }
        tft.setTextSize(1);
        tft.setCursor(6, y);
        tft.print(userNames[i]);
        // Show password as dots
        tft.setTextColor(i == adminPwdUser ? COL_VALUE : COL_DIVIDER);
        tft.setCursor(TFT_WIDTH - 40, y);
        for (int j = 0; j < USER_PASSWORD_LEN; j++) tft.print("*");
    }
    drawKeyHints("UP", "DN", "BACK", "EDIT");
}

void drawAdminPasswordEditScreen() {
    // screen cleared by refreshDisplay() on transition
    drawHeader("SET PASSWORD");

    tft.setTextSize(1);
    tft.setTextColor(COL_ACCENT);
    tft.setCursor(4, 24);
    tft.print(userNames[adminPwdUser]);

    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 38);
    tft.print("Enter new password:");

    static const char* keyLabels[] = {"0", "1", "2", "3"};
    for (int i = 0; i < USER_PASSWORD_LEN; i++) {
        tft.setTextSize(2);
        if (i < (int)newPwdPos) {
            tft.setTextColor(COL_VALUE);
            tft.setCursor(16 + i * 26, 54);
            tft.print(keyLabels[newPwdBuffer[i]]);
        } else {
            tft.setTextColor(COL_DIVIDER);
            tft.setCursor(16 + i * 26, 54);
            tft.print("-");
        }
    }

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 90);
    tft.print("Current:");
    if (userNoPassword(adminPwdUser)) {
        tft.setTextColor(COL_WARN);
        tft.setCursor(56, 90);
        tft.print("NO PASS");
    } else {
        tft.setTextColor(COL_DIVIDER);
        tft.setCursor(56, 90);
        for (int j = 0; j < USER_PASSWORD_LEN; j++) tft.print("*");
    }

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 108);
    if (newPwdPos == 0)
        tft.print("#=CLEAR  else:0-3");
    else
        tft.print("^=0  v=1  #=2  *=3");

    drawKeyHints("0", "1", newPwdPos == 0 ? "CLR" : "2", "3");
}

void drawStrokeGapScreen() {
    drawHeader("STROKE GAP");

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(4, 24);
    tft.print("Gap between strokes:");

    tft.setTextSize(3);
    tft.setTextColor(COL_ACCENT, COL_BG);
    tft.fillRect(0, 38, TFT_WIDTH, 28, COL_BG);
    tft.setCursor(8, 40);
    tft.printf("%4ums", (unsigned)strokeGapMs);

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(4, 75);
    tft.print("Step: 50ms  (200-3000)");

    drawDivider(90);

    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(4, 95);
    tft.print("Default:");
    tft.setTextColor(COL_VALUE, COL_BG);
    tft.setCursor(60, 95);
    tft.printf("%ums", STROKE_GAP_MS);

    drawKeyHints("+50", "-50", "BACK", "SAVE");
}

void drawCalibAuthScreen() {
    // screen cleared by refreshDisplay() on transition
    drawHeader("ADMIN ACCESS");

    tft.drawBitmap(56, 22, bmp_skull, 16, 16, COL_WARN);

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 44);
    tft.print("Enter password:");

    // Show entered keys as their digit (0/1/2/3) for entered, dash for remaining
    static const char* keyLabels[] = {"0", "1", "2", "3"};
    for (int i = 0; i < CALIB_PASSWORD_LEN; i++) {
        tft.setTextSize(2);
        if (i < (int)pwdPos) {
            tft.setTextColor(COL_ACCENT);
            tft.setCursor(16 + i * 26, 58);
            tft.print(keyLabels[pwdBuffer[i]]);
        } else {
            tft.setTextColor(COL_DIVIDER);
            tft.setCursor(16 + i * 26, 58);
            tft.print("-");
        }
    }

    if (pwdWrong) {
        tft.setTextSize(1);
        tft.setTextColor(COL_ERROR);
        tft.setCursor(4, 90);
        tft.printf("WRONG! %d/3 attempts", pwdWrongCount);
    }

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 108);
    tft.print("^=0  v=1  #=2  *=3");

    drawKeyHints("0", "1", "2", "3");
}

void drawCalibScreen() {
    // screen cleared by refreshDisplay() on transition
    drawHeader("CALIBRATION");

    uint32_t counted = pulseCount - calibPulseStart;

    // Live pulse counter — big and prominent
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(4, 22);
    tft.print("PULSES (pull rope):");
    tft.setTextSize(3);
    tft.setTextColor(counted > 0 ? COL_ACCENT : COL_DIVIDER, COL_BG);
    tft.setCursor(4, 34);
    tft.printf("%5lu", (unsigned long)counted);

    // Calculated m/pulse — clear the whole region first (content changes)
    tft.fillRect(0, 63, TFT_WIDTH, 28, COL_BG);
    tft.setTextSize(1);
    uint16_t refM = calibRefOptions[calibRefIdx];
    if (counted > 0) {
        tft.setTextColor(COL_WARN, COL_BG);
        tft.setCursor(4, 65);
        tft.printf("Ref %um / pulses:", (unsigned)refM);
        tft.setTextColor(COL_VALUE, COL_BG);
        tft.setCursor(4, 76);
        tft.printf("%lu=%.8f", (unsigned long)counted, (float)refM / counted);
    } else {
        tft.setTextColor(COL_LABEL, COL_BG);
        tft.setCursor(4, 68);
        tft.printf("Pull rope (ref %um)", (unsigned)refM);
    }

    drawDivider(90);

    // Current set value
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(4, 95);
    tft.print("SET m/pulse:");
    tft.setTextColor(COL_VALUE, COL_BG);
    tft.setCursor(4, 107);
    tft.printf("%.8f", calMetersPerPulse);

    // Key hints
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setCursor(4, 122);
    tft.print("UP/DN=ref  #=RST  *=SAVE");

    drawKeyHints("ref+", "ref-", counted > 0 ? "RESET" : "BACK", "SAVE");
}

void drawUserSelectScreen() {
    // screen cleared by refreshDisplay() on transition
    drawHeader("WHO ARE YOU?");

    // scroll up indicator
    if (userScrollOffset > 0) {
        tft.setTextColor(COL_LABEL);
        tft.setTextSize(1);
        tft.setCursor(60, 20);
        tft.print("^");  // up
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
            tft.setTextColor(isGuest ? COL_WARN : COL_ACCENT, COL_HEADER_BG);
            tft.setCursor(6, y);
            tft.print("> ");
        } else {
            tft.fillRect(2, y - 2, TFT_WIDTH - 4, 16, COL_BG);
            tft.setTextColor(isGuest ? COL_WARN : COL_LABEL, COL_BG);
            tft.setCursor(6, y);
            tft.print("  ");
        }
        tft.setTextSize(1);
        tft.print(name);

        // counter on right (Guest shows no number)
        if (!isGuest) {
            tft.setTextColor(COL_DIVIDER, COL_BG);
            tft.setCursor(TFT_WIDTH - 20, y);
            tft.printf("%d/%d", i + 1, USER_COUNT);
        }
    }

    // scroll down indicator
    if (userScrollOffset + USER_VISIBLE < USER_COUNT + 1) {
        tft.setTextColor(COL_LABEL);
        tft.setTextSize(1);
        tft.setCursor(60, 140);
        tft.print("v");  // down
    }

    drawKeyHints("UP", "DN", "BACK", "GO");
}

void drawUserAuthScreen() {
    // screen cleared by refreshDisplay() on transition
    drawHeader("USER LOGIN");

    tft.drawBitmap(56, 22, bmp_rower, 16, 16, COL_ACCENT);

    tft.setTextSize(1);
    tft.setTextColor(COL_ACCENT);
    tft.setCursor(4, 44);
    tft.print(userNames[currentUser]);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 54);
    tft.print("Enter password:");

    static const char* keyLabels[] = {"0", "1", "2", "3"};
    for (int i = 0; i < USER_PASSWORD_LEN; i++) {
        tft.setTextSize(2);
        if (i < (int)userPwdPos) {
            tft.setTextColor(COL_ACCENT);
            tft.setCursor(16 + i * 26, 58);
            tft.print(keyLabels[userPwdBuffer[i]]);
        } else {
            tft.setTextColor(COL_DIVIDER);
            tft.setCursor(16 + i * 26, 58);
            tft.print("-");
        }
    }

    if (userPwdWrong) {
        tft.setTextSize(1);
        tft.setTextColor(COL_ERROR);
        tft.setCursor(4, 90);
        tft.printf("WRONG! %d/3 attempts", userWrongCount[currentUser]);
    }

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 108);
    tft.print("^=0  v=1  #=2  *=3");

    drawKeyHints("0", "1", "2", "3");
}

void drawGuestWeightScreen() {
    // screen cleared by refreshDisplay() on transition
    drawHeader("GUEST WEIGHT");

    tft.setTextSize(1);
    tft.setTextColor(COL_ACCENT);
    tft.setCursor(4, 24);
    tft.print("Guest");

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 38);
    tft.print("Weight (not saved):");

    tft.setTextSize(3);
    tft.setTextColor(COL_WARN);
    tft.setCursor(20, 60);
    tft.printf("%.0f", userWeights[USER_COUNT]);
    tft.setTextSize(2);
    tft.print(" kg");

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 110);
    tft.print("^ +1 kg    v -1 kg");

    drawKeyHints("+1", "-1", "BACK", "GO");
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
    persistHistory();
}

// ───────── Reset ─────────
void resetWorkout() {
    totalPulses = 0; totalMeters = 0; totalCalories = 0;
    strokeCount = 0; currentSpeed = 0; strokeRate = 0;
    workoutElapsedMs = 0; lastPulseMs = 0;
    lastStrokeBoundary = 0; inStroke = false;
    smoothedIntervalMs = 0; bgIntervalMs = 0; inDrive = false;
    pulseCount = 0; displayPage = 0;
    lastPulseIntervalMs = 0;
#ifdef SENSOR_TYPE_LDR
    ldrPulseActive = false;
#endif
#ifdef SENSOR_TYPE_HALL
    hallPulseCount = 0;
#endif
#ifdef SENSOR_TYPE_BLOCKER
    blockerPulseCount = 0;
#endif
}

// ───────── Process sensor data ─────────
void processSensor() {
    if (!workoutActive && currentScreen != SCR_CALIB) return;

    if (pulseCount > totalPulses) {
        uint32_t newPulses = pulseCount - totalPulses;
        totalPulses = pulseCount;
        totalMeters += newPulses * calMetersPerPulse;
        // calories: weight-based estimate (0.571 kcal per kg per km)
        totalCalories = totalMeters * userWeights[currentUser] * 0.000571f;

        // Speed from last pulse interval
        if (lastPulseIntervalMs > 0) {
            currentSpeed = calMetersPerPulse / (lastPulseIntervalMs / 1000.0f);
        }

        // Stroke detection: dual-EMA speed comparison.
        // fastEma (α=0.25) tracks instantaneous speed.
        // bgEma   (α=0.01) tracks background average (very slow, ~1-2s time constant).
        // Drive = fastEma significantly faster than background → new stroke start.
        if (lastPulseIntervalMs > 0) {
            if (smoothedIntervalMs == 0) { smoothedIntervalMs = bgIntervalMs = lastPulseIntervalMs; }
            smoothedIntervalMs = smoothedIntervalMs * 0.75f + lastPulseIntervalMs * 0.25f;
            bgIntervalMs       = bgIntervalMs       * 0.99f + lastPulseIntervalMs * 0.01f;

            // Drive: fast EMA < 72% of background = flywheel 28%+ faster than average
            bool driving = (bgIntervalMs > 0 && smoothedIntervalMs < bgIntervalMs * 0.72f);
            if (!inDrive && driving) {
                unsigned long now = millis();
                if (lastStrokeBoundary == 0 || now - lastStrokeBoundary > (unsigned long)strokeGapMs) {
                    if (lastStrokeBoundary > 0)
                        strokeRate = 60000.0f / (now - lastStrokeBoundary);
                    lastStrokeBoundary = now;
                    strokeCount++;
                }
            }
            inDrive = driving;
        }
    }

    // Idle detection
    if (lastPulseMs > 0 && (millis() - lastPulseMs > IDLE_TIMEOUT_MS)) {
        currentSpeed = 0;
        strokeRate = 0;
    }
}

// ───────── Handle input ─────────
void handleInput() {
    switch (currentScreen) {
    case SCR_IDLE: {
        // Capture all presses first, then decide action
        bool up   = consume(btnUp);
        bool down = consume(btnDown);
        bool hash = consume(btnHash);
        bool star = consume(btnStar);

        // Push every press into combo sliding window
        if (up)   { pushCombo(0); brightness = min(255, brightness + 25); setBacklight(brightness); }
        if (down) { pushCombo(1); brightness = max((uint8_t)10, (uint8_t)(brightness - 25)); setBacklight(brightness); }
        if (hash)   pushCombo(2);
        if (star)   pushCombo(3);

        // Combo check runs after EVERY key — works regardless of which key ends the combo
        if (checkCombo()) {
            memset(comboBuffer, 0, sizeof(comboBuffer));
            comboPos = 0;
            if (millis() < pwdLockUntilMs) {
                playTone(150, 400);
            } else {
                pwdPos = 0; pwdWrong = false;
                beep(); delay(80); beep();
                currentScreen = SCR_CALIB_AUTH;
            }
            break;
        }

        // Normal navigation only if combo didn't fire
        if (hash) { beep(); displayPage = 0; currentScreen = SCR_HISTORY; }
        if (star) { beep(); currentUser = 0; userScrollOffset = 0; currentScreen = SCR_USER_SELECT; }
        break;
    }

    case SCR_CALIB_AUTH:
        {
            if (millis() < pwdLockUntilMs) {
                consume(btnUp); consume(btnDown); consume(btnHash); consume(btnStar);
                currentScreen = SCR_IDLE;
                break;
            }
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
                        pwdWrongCount = 0;
                        playTone(1600, 100);
                        adminMenuItem = 0;
                        currentScreen = SCR_ADMIN_MENU;
                    } else {
                        if (pwdWrongCount < 3) {
                            String ts = getTimestamp();
                            strncpy(pwdAttemptTimes[pwdWrongCount], ts.c_str(), 31);
                            memcpy(pwdAttemptKeys[pwdWrongCount], pwdBuffer, CALIB_PASSWORD_LEN);
                        }
                        pwdWrongCount++;
                        pwdWrong = true;
                        pwdPos = 0;
                        if (pwdWrongCount >= 3) {
                            pwdLockUntilMs = millis() + 300000UL;  // 5 min
                            pwdWrongCount = 0;
                            addSuspicious("ADMIN");
                            playTone(150, 600);
                            uploadBadAttempt();
                        } else {
                            playTone(200, 300);
                        }
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
            if (adminMenuItem == 0) {
                readSensor();               // sync pulseCount from ISR before snapshotting
                calibPulseStart = pulseCount;
                currentScreen = SCR_CALIB;
            }
            else if (adminMenuItem == 1) { adminEditUser = 0; currentScreen = SCR_ADMIN_WEIGHTS; }
            else if (adminMenuItem == 2) { adminPwdUser = 0; currentScreen = SCR_ADMIN_PASSWORDS; }
            else if (adminMenuItem == 3) { currentScreen = SCR_ADMIN_STROKE_GAP; }
        }
        break;

    case SCR_CALIB: {
        uint32_t counted = pulseCount - calibPulseStart;
        if (consume(btnUp))   { if (calibRefIdx < calibRefCount - 1) calibRefIdx++; beep(); }
        if (consume(btnDown)) { if (calibRefIdx > 0) calibRefIdx--; beep(); }
        if (consume(btnStar)) {
            if (counted > 0)
                calMetersPerPulse = (float)calibRefOptions[calibRefIdx] / counted;
            saveCalibration();
            playTone(TONE_UPLOAD, 200);
            currentScreen = SCR_ADMIN_MENU;
        }
        if (consume(btnHash)) {
            if (counted > 0) {
                calibPulseStart = pulseCount;  // reset counter
                beep();
            } else {
                loadCalibration();
                beep();
                currentScreen = SCR_ADMIN_MENU;
            }
        }
        break;
    }

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

    case SCR_ADMIN_PASSWORDS:
        if (consume(btnUp))   { if (adminPwdUser > 0) adminPwdUser--; }
        if (consume(btnDown)) { if (adminPwdUser < USER_COUNT - 1) adminPwdUser++; }
        if (consume(btnHash)) { beep(); currentScreen = SCR_ADMIN_MENU; }
        if (consume(btnStar)) {
            beep();
            newPwdPos = 0;
            memset(newPwdBuffer, 0, sizeof(newPwdBuffer));
            currentScreen = SCR_ADMIN_PASSWORD_EDIT;
        }
        break;

    case SCR_ADMIN_PASSWORD_EDIT:
        {
            // # before any digit = clear password (no password required)
            if (newPwdPos == 0 && consume(btnHash)) {
                memset(userPasswords[adminPwdUser], 0xFF, USER_PASSWORD_LEN);
                savePassword(adminPwdUser);
                playTone(TONE_UPLOAD, 200);
                currentScreen = SCR_ADMIN_PASSWORDS;
                break;
            }
            uint8_t pressed = 0xFF;
            if (consume(btnUp))   pressed = 0;
            if (consume(btnDown)) pressed = 1;
            if (consume(btnHash)) pressed = 2;
            if (consume(btnStar)) pressed = 3;
            if (pressed != 0xFF) {
                beep();
                newPwdBuffer[newPwdPos++] = pressed;
                if (newPwdPos >= USER_PASSWORD_LEN) {
                    memcpy(userPasswords[adminPwdUser], newPwdBuffer, USER_PASSWORD_LEN);
                    savePassword(adminPwdUser);
                    playTone(TONE_UPLOAD, 200);
                    currentScreen = SCR_ADMIN_PASSWORDS;
                }
            }
        }
        break;

    case SCR_ADMIN_STROKE_GAP:
        if (consume(btnUp))   { strokeGapMs = min((uint16_t)3000, (uint16_t)(strokeGapMs + 50)); beep(); }
        if (consume(btnDown)) { strokeGapMs = max((uint16_t)200,  (uint16_t)(strokeGapMs - 50)); beep(); }
        if (consume(btnStar)) { saveCalibration(); playTone(TONE_UPLOAD, 200); currentScreen = SCR_ADMIN_MENU; }
        if (consume(btnHash)) { loadCalibration(); beep(); currentScreen = SCR_ADMIN_MENU; }
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
            if (currentUser == USER_COUNT) {
                userWeights[USER_COUNT] = DEFAULT_WEIGHT_KG;
                currentScreen = SCR_GUEST_WEIGHT;
            } else if (millis() < userLockUntilMs[currentUser]) {
                playTone(150, 400);
                currentScreen = SCR_IDLE;
            } else if (userNoPassword(currentUser)) {
                resetWorkout();
                workoutActive = true;
                workoutStartMs = millis();
                currentScreen = SCR_WORKOUT;
                playTone(TONE_START, TONE_DURATION);
            } else {
                userPwdPos = 0;
                userPwdWrong = false;
                memset(userPwdBuffer, 0, sizeof(userPwdBuffer));
                currentScreen = SCR_USER_AUTH;
            }
        }
        break;

    case SCR_USER_AUTH:
        {
            if (millis() < userLockUntilMs[currentUser]) {
                consume(btnUp); consume(btnDown); consume(btnHash); consume(btnStar);
                currentScreen = SCR_IDLE;
                break;
            }
            uint8_t pressed = 0xFF;
            if (consume(btnUp))   pressed = 0;
            if (consume(btnDown)) pressed = 1;
            if (consume(btnHash)) { beep(); currentScreen = SCR_USER_SELECT; break; }
            if (consume(btnStar)) pressed = 3;
            if (pressed != 0xFF) {
                beep();
                userPwdWrong = false;
                userPwdBuffer[userPwdPos++] = pressed;
                if (userPwdPos >= USER_PASSWORD_LEN) {
                    bool ok = true;
                    for (int i = 0; i < USER_PASSWORD_LEN; i++)
                        if (userPwdBuffer[i] != userPasswords[currentUser][i]) { ok = false; break; }
                    if (ok) {
                        userWrongCount[currentUser] = 0;
                        resetWorkout();
                        workoutActive = true;
                        workoutStartMs = millis();
                        currentScreen = SCR_WORKOUT;
                        playTone(TONE_START, TONE_DURATION);
                    } else {
                        int idx = userWrongCount[currentUser];
                        if (idx < 3) {
                            String ts = getTimestamp();
                            strncpy(userAttemptTimes[currentUser][idx], ts.c_str(), 31);
                            memcpy(userAttemptKeys[currentUser][idx], userPwdBuffer, USER_PASSWORD_LEN);
                        }
                        userWrongCount[currentUser]++;
                        userPwdWrong = true;
                        userPwdPos = 0;
                        if (userWrongCount[currentUser] >= 3) {
                            userLockUntilMs[currentUser] = millis() + 300000UL;
                            userWrongCount[currentUser] = 0;
                            addSuspicious(userNames[currentUser]);
                            playTone(150, 600);
                            uploadUserBadAttempt(currentUser);
                            currentScreen = SCR_IDLE;
                        } else {
                            playTone(200, 300);
                        }
                    }
                }
            }
        }
        break;

    case SCR_GUEST_WEIGHT:
        if (consume(btnUp))   { userWeights[USER_COUNT] = min(200.0f, userWeights[USER_COUNT] + 1.0f); }
        if (consume(btnDown)) { userWeights[USER_COUNT] = max(20.0f,  userWeights[USER_COUNT] - 1.0f); }
        if (consume(btnHash)) { beep(); currentScreen = SCR_USER_SELECT; }
        if (consume(btnStar)) {
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
        if (!lastUploadOk && currentUser != USER_COUNT && consume(btnStar)) {
            tft.fillScreen(COL_BG);
            currentScreen = SCR_UPLOADING;  // retry
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
#define REDRAW_MS 1000
Screen lastDrawnScreen = (Screen)-1;

void refreshDisplay() {
    bool screenChanged = (currentScreen != lastDrawnScreen);
    if (!screenChanged && millis() - lastRedraw < REDRAW_MS) return;
    lastRedraw = millis();
    if (screenChanged) {
        tft.fillScreen(COL_BG);
        lastDrawnScreen = currentScreen;
    }

    switch (currentScreen) {
        case SCR_IDLE:        drawIdleScreen(); break;
        case SCR_USER_SELECT:  drawUserSelectScreen(); break;
        case SCR_USER_AUTH:    drawUserAuthScreen(); break;
        case SCR_GUEST_WEIGHT: drawGuestWeightScreen(); break;
        case SCR_WORKOUT:      drawWorkoutScreen(); break;
        case SCR_PAUSED:      drawPausedScreen(); break;
        case SCR_HISTORY:     drawHistoryScreen(); break;
        case SCR_CALIB_AUTH:        drawCalibAuthScreen(); break;
        case SCR_ADMIN_MENU:        drawAdminMenuScreen(); break;
        case SCR_CALIB:             drawCalibScreen(); break;
        case SCR_ADMIN_WEIGHTS:         drawAdminWeightsScreen(); break;
        case SCR_ADMIN_WEIGHT_EDIT:     drawAdminWeightEditScreen(); break;
        case SCR_ADMIN_PASSWORDS:       drawAdminPasswordsScreen(); break;
        case SCR_ADMIN_PASSWORD_EDIT:   drawAdminPasswordEditScreen(); break;
        case SCR_ADMIN_STROKE_GAP:      drawStrokeGapScreen(); break;
        default: break;
    }
}

// ───────── Setup ─────────
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // disable brownout reset
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

    // Load saved calibration and passwords from NVS
    loadCalibration();
    loadPasswords();
    loadHistory();

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

    // Serial sensor debug — prints every 500ms
    static unsigned long lastDbg = 0;
    if (millis() - lastDbg >= 500) {
        lastDbg = millis();
#ifdef SENSOR_TYPE_BLOCKER
        noInterrupts();
        unsigned long isr = blockerPulseCount;
        interrupts();
        Serial.printf("[SENSOR] pin=%d ISR=%lu pulse=%lu strokes=%lu spm=%.1f fast=%.1f bg=%.1f drive=%d scr=%d\n",
            BLOCKER_SENSOR_PIN, isr, (unsigned long)pulseCount, (unsigned long)strokeCount,
            strokeRate, smoothedIntervalMs, bgIntervalMs, (int)inDrive, (int)currentScreen);
#elif defined(SENSOR_TYPE_HALL)
        noInterrupts();
        unsigned long isr = hallPulseCount;
        interrupts();
        Serial.printf("[SENSOR] pin=%d raw=%d ISR_count=%lu pulseCount=%lu screen=%d\n",
            HALL_SENSOR_PIN, digitalRead(HALL_SENSOR_PIN),
            isr, (unsigned long)pulseCount, (int)currentScreen);
#elif defined(SENSOR_TYPE_LDR)
        Serial.printf("[SENSOR] analog=%d threshold=%d pulseCount=%lu screen=%d\n",
            analogRead(LDR_SIGNAL_PIN), LDR_THRESHOLD,
            (unsigned long)pulseCount, (int)currentScreen);
#endif
    }

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
            tft.fillScreen(COL_BG);
            drawUploadingScreen();
            ok = uploadToGitHub(durSec);
            if (ok) {
                playTone(TONE_UPLOAD, 200);
            } else {
                playTone(300, 500);
            }
        }
        lastUploadOk = ok;
        tft.fillScreen(COL_BG);
        drawSummaryScreen(ok);
        currentScreen = SCR_SUMMARY;
        lastDrawnScreen = SCR_SUMMARY;
        return;
    }

    refreshDisplay();
}
