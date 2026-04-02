/*
 * Water Rower Monitor — ESP32-S3
 * Replacement console for Water Rower USA (SN 132224)
 *
 * Hardware:
 *   - ESP32-S3 DevKitC-1
 *   - ST7735S 1.8" 128x160 TFT with 4 built-in keys
 *   - LDR (光敏电阻) — original WaterRower sensor via 4-pin JST
 *   - Speaker (喇叭) — for sound feedback
 *   - Large half breadboard (165x55mm)
 *
 * Sensor: The WaterRower uses an optical sensor (LDR + IR LED).
 * When a flywheel paddle passes, it blocks light → LDR value drops.
 * We read the LDR with analogRead and detect pulse edges.
 *
 * Keys (on TFT module):
 *   * (K4)  = Start / Resume
 *   # (K3)  = Stop & save / Back
 *   UP (K1) = Scroll up / Brightness+
 *   DN (K2) = Scroll down / Brightness-
 */

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include "config.h"

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

// ───────── Display ─────────
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
uint8_t brightness = 200;

// ───────── State machine ─────────
enum Screen { SCR_IDLE, SCR_WORKOUT, SCR_PAUSED, SCR_SUMMARY, SCR_UPLOADING, SCR_HISTORY };
Screen currentScreen = SCR_IDLE;
int displayPage = 0;

// ───────── LDR Sensor State ─────────
bool     ldrPulseActive = false;   // currently in a "blocked" state
uint32_t pulseCount     = 0;
unsigned long lastPulseMs = 0;
unsigned long lastPulseIntervalMs = 0;

// ───────── Workout state ─────────
bool     workoutActive  = false;
uint32_t totalPulses    = 0;
float    totalMeters    = 0;
float    totalCalories  = 0;
uint32_t strokeCount    = 0;
float    currentSpeed   = 0;
float    strokeRate     = 0;
unsigned long workoutStartMs   = 0;
unsigned long workoutElapsedMs = 0;
unsigned long lastStrokeBoundary = 0;
bool     inStroke = false;

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
    ledcWriteTone(SPEAKER_PIN, freq);
    delay(durationMs);
    ledcWriteTone(SPEAKER_PIN, 0);
}

void beep() {
    playTone(TONE_BEEP, 50);
}

// ───────── Backlight ─────────
void setBacklight(uint8_t val) {
    if (TFT_BL >= 0) {
        ledcWrite(TFT_BL, val);
    }
}

// ───────── LDR Sensor Reading ─────────
void readLDR() {
    if (!workoutActive) return;

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

// ───────── WiFi ─────────
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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

// ───────── GitHub Upload ─────────
bool uploadToGitHub(uint32_t durSec) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return false;

    StaticJsonDocument<1024> doc;
    doc["machine_sn"]      = MACHINE_SN;
    doc["machine_model"]   = MACHINE_MODEL;
    doc["date"]            = getTimestamp();
    doc["duration_sec"]    = durSec;
    doc["distance_m"]      = totalMeters;
    doc["strokes"]         = strokeCount;
    doc["calories"]        = totalCalories;
    doc["avg_speed_ms"]    = (durSec > 0) ? totalMeters / durSec : 0;
    doc["avg_stroke_rate"] = (durSec > 0) ? strokeCount * 60.0f / durSec : 0;
    if (totalMeters > 0) {
        float secPer500 = (durSec * 500.0f) / totalMeters;
        int sp_min = (int)(secPer500 / 60);
        int sp_sec = (int)secPer500 % 60;
        char split[16];
        snprintf(split, sizeof(split), "%d:%02d", sp_min, sp_sec);
        doc["split_500m"] = split;
    }

    String content;
    serializeJsonPretty(doc, content);

    size_t encodedLen = 0;
    mbedtls_base64_encode(NULL, 0, &encodedLen,
        (const unsigned char*)content.c_str(), content.length());
    char* encoded = (char*)malloc(encodedLen + 1);
    if (!encoded) return false;
    mbedtls_base64_encode((unsigned char*)encoded, encodedLen + 1, &encodedLen,
        (const unsigned char*)content.c_str(), content.length());
    encoded[encodedLen] = '\0';

    String ts = getTimestamp();
    ts.replace("-", ""); ts.replace(":", "");
    ts.replace("T", "_"); ts.replace("Z", "");
    String filename = "workouts/workout_" + ts + ".json";

    String url = "https://api.github.com/repos/";
    url += GITHUB_OWNER; url += "/";
    url += GITHUB_REPO;  url += "/contents/"; url += filename;

    StaticJsonDocument<2048> apiDoc;
    apiDoc["message"] = "Workout " + getTimestamp() + " | " +
                        String(totalMeters, 0) + "m " +
                        String(durSec / 60) + "min";
    apiDoc["content"] = encoded;
    apiDoc["branch"]  = GITHUB_BRANCH;
    String apiBody;
    serializeJson(apiDoc, apiBody);
    free(encoded);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Authorization", String("Bearer ") + GITHUB_TOKEN);
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("Content-Type", "application/json");
    int code = http.PUT(apiBody);
    http.end();

    return (code == 201);
}

// ───────── Drawing Helpers ─────────
void drawHeader(const char* title) {
    tft.fillRect(0, 0, TFT_WIDTH, 18, COL_HEADER_BG);
    tft.setTextSize(1);
    tft.setTextColor(COL_ACCENT);
    tft.setCursor(4, 5);
    tft.print(title);
    tft.setCursor(TFT_WIDTH - 18, 5);
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

    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(4, 26);
    tft.print("SN: " MACHINE_SN);
    tft.setCursor(4, 40);
    tft.setTextColor(COL_LABEL);
    tft.print(MACHINE_MODEL);

    drawDivider(55);

    // Show current LDR value for debugging/calibration
    int ldrVal = analogRead(LDR_SIGNAL_PIN);
    tft.setCursor(4, 60);
    tft.setTextColor(COL_LABEL);
    tft.setTextSize(1);
    tft.printf("LDR: %d (thr:%d)", ldrVal, LDR_THRESHOLD);

    tft.setCursor(4, 78);
    tft.setTextColor(COL_ACCENT);
    tft.setTextSize(2);
    tft.print("READY");

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 104);
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

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mn, s);
    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(30, 22);
    tft.print(timeBuf);

    drawDivider(42);

    char distBuf[16];
    snprintf(distBuf, sizeof(distBuf), "%5.0f", totalMeters);
    drawLabelValue(4, 46, "DISTANCE (m)", distBuf);

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

    drawDivider(106);
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 110);  tft.print("SPM");
    tft.setCursor(68, 110); tft.print("CAL");
    tft.setTextSize(2);
    tft.setTextColor(COL_VALUE);
    tft.setCursor(4, 122);  tft.printf("%-3.0f", strokeRate);
    tft.setCursor(68, 122); tft.printf("%.0f", totalCalories);

    drawKeyHints("", "", "STOP", "");
}

void drawPausedScreen() {
    tft.fillScreen(COL_BG);
    drawHeader("PAUSED");

    uint32_t sec = workoutElapsedMs / 1000;
    uint32_t mn = sec / 60;
    uint32_t s  = sec % 60;

    tft.setTextSize(2);
    tft.setTextColor(COL_WARN);
    tft.setCursor(14, 28);
    tft.print("PAUSED");

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mn, s);
    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(30, 52);
    tft.print(timeBuf);

    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 78);  tft.printf("Dist: %.0f m", totalMeters);
    tft.setCursor(4, 92);  tft.printf("Strokes: %d", strokeCount);
    tft.setCursor(4, 106); tft.printf("Cal: %.0f", totalCalories);

    drawKeyHints("", "", "SAVE", "RESUME");
}

void drawSummaryScreen(bool uploadOk) {
    tft.fillScreen(COL_BG);
    drawHeader("SUMMARY");

    uint32_t sec = workoutElapsedMs / 1000;
    uint32_t mn = sec / 60;
    uint32_t s  = sec % 60;
    char buf[32];

    snprintf(buf, sizeof(buf), "%02d:%02d", mn, s);
    drawLabelValue(4, 24, "TIME", buf, COL_TEXT);

    snprintf(buf, sizeof(buf), "%.0f", totalMeters);
    drawLabelValue(4, 54, "DISTANCE (m)", buf);

    snprintf(buf, sizeof(buf), "%d", strokeCount);
    drawLabelValue(4, 84, "STROKES", buf, COL_ACCENT);

    snprintf(buf, sizeof(buf), "%.0f", totalCalories);
    drawLabelValue(68, 84, "CAL", buf, COL_WARN);

    drawDivider(114);
    tft.setTextSize(1);
    tft.setCursor(4, 120);
    if (uploadOk) {
        tft.setTextColor(COL_VALUE);
        tft.print("Uploaded to GitHub OK");
    } else {
        tft.setTextColor(COL_ERROR);
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
    tft.setTextSize(2);
    tft.setTextColor(COL_WARN);
    tft.setCursor(4, 50);
    tft.print("Uploading");
    tft.setTextSize(1);
    tft.setTextColor(COL_LABEL);
    tft.setCursor(4, 80);
    tft.print("to GitHub...");
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
    lastStrokeBoundary = 0; inStroke = false;
    pulseCount = 0; displayPage = 0;
    ldrPulseActive = false; lastPulseIntervalMs = 0;
}

// ───────── Process sensor data ─────────
void processSensor() {
    if (!workoutActive) return;

    if (pulseCount > totalPulses) {
        uint32_t newPulses = pulseCount - totalPulses;
        totalPulses = pulseCount;
        totalMeters += newPulses * METERS_PER_PULSE;
        totalCalories = totalMeters * CAL_PER_METER;

        // Speed from last pulse interval
        if (lastPulseIntervalMs > 0) {
            currentSpeed = METERS_PER_PULSE / (lastPulseIntervalMs / 1000.0f);
        }

        // Stroke detection
        unsigned long now = millis();
        if (!inStroke) {
            inStroke = true;
            if (lastStrokeBoundary > 0) {
                unsigned long strokeInterval = now - lastStrokeBoundary;
                if (strokeInterval > 0) {
                    strokeRate = 60000.0f / strokeInterval;
                }
            }
            lastStrokeBoundary = now;
            strokeCount++;
        }
    }

    // Stroke boundary (gap in pulses)
    if (inStroke && lastPulseMs > 0 && (millis() - lastPulseMs > STROKE_GAP_MS)) {
        inStroke = false;
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
    case SCR_IDLE:
        if (consume(btnStar)) {
            beep();
            resetWorkout();
            workoutActive = true;
            workoutStartMs = millis();
            currentScreen = SCR_WORKOUT;
            playTone(TONE_START, TONE_DURATION);
        }
        if (consume(btnHash)) {
            beep();
            displayPage = 0;
            currentScreen = SCR_HISTORY;
        }
        if (consume(btnUp)) {
            brightness = min(255, brightness + 25);
            setBacklight(brightness);
        }
        if (consume(btnDown)) {
            brightness = max((uint8_t)10, (uint8_t)(brightness - 25));
            setBacklight(brightness);
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
        case SCR_IDLE:    drawIdleScreen(); break;
        case SCR_WORKOUT: drawWorkoutScreen(); break;
        case SCR_PAUSED:  drawPausedScreen(); break;
        case SCR_HISTORY: drawHistoryScreen(); break;
        default: break;
    }
}

// ───────── Setup ─────────
void setup() {
    Serial.begin(115200);

    // Backlight
    if (TFT_BL >= 0) {
        ledcAttach(TFT_BL, 5000, 8);
        setBacklight(brightness);
    }

    // Speaker
    ledcAttach(SPEAKER_PIN, 2000, 8);

    // TFT
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(0);
    tft.fillScreen(COL_BG);
    tft.setTextWrap(false);

    // Splash
    tft.setTextSize(1);
    tft.setTextColor(COL_ACCENT);
    tft.setCursor(10, 40);
    tft.println("WATER ROWER");
    tft.setCursor(10, 55);
    tft.println("MONITOR v2.0");
    tft.setCursor(10, 80);
    tft.setTextColor(COL_LABEL);
    tft.println("LDR Sensor + Speaker");

    // Pins
    pinMode(LDR_SIGNAL_PIN, INPUT);
    analogSetAttenuation(ADC_11db);  // full 0-3.3V range
    pinMode(BTN_UP_PIN,   INPUT_PULLUP);
    pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
    pinMode(BTN_HASH_PIN, INPUT_PULLUP);
    pinMode(BTN_STAR_PIN, INPUT_PULLUP);

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

    // Read LDR sensor (fast polling, not interrupt-based for analog)
    readLDR();

    processSensor();

    // Upload (blocking)
    if (currentScreen == SCR_UPLOADING) {
        drawUploadingScreen();
        uint32_t durSec = workoutElapsedMs / 1000;
        saveToHistory(durSec);
        bool ok = uploadToGitHub(durSec);
        if (ok) {
            playTone(TONE_UPLOAD, 200);
        } else {
            playTone(300, 500);
        }
        drawSummaryScreen(ok);
        currentScreen = SCR_SUMMARY;
        lastDrawnScreen = SCR_SUMMARY;
        return;
    }

    refreshDisplay();
}
