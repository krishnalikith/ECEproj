// ============================================================
//  Library VOC Degradation Monitor — ESP32 Complete Code
//  Project: Autonomous Library Collection Degradation Monitoring
//  Inventor: Likith Krishna Nannapanenu
//  Institution: Lovely Professional University
// ============================================================
//
//  PIN CONNECTIONS:
//  MQ-135  → GPIO34 (Analog input)
//  DHT22   → GPIO4  (Digital)
//  BH1750  → SDA = GPIO21, SCL = GPIO22 (I2C)
//  LCD     → SDA = GPIO21, SCL = GPIO22 (I2C, same bus as BH1750)
//  Green LED → GPIO25
//  Red LED   → GPIO26
//  Buzzer    → GPIO27
//
//  LIBRARIES TO INSTALL (Tools → Manage Libraries):
//  1. DHT sensor library by Adafruit
//  2. Adafruit Unified Sensor by Adafruit
//  3. BH1750 by Christopher Laws
//  4. LiquidCrystal I2C by Frank de Brabander
// ============================================================

#include <Wire.h>
#include <DHT.h>
#include <BH1750.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ─── WiFi Credentials ──────────────────────────────────────
#define WIFI_SSID     "LIKITH"
#define WIFI_PASSWORD "123456789"

// ─── Telegram Bot ───────────────────────────────────────────
#define BOT_TOKEN  "8411750205:AAHCIK0urxGe6KFAZldjm5mBEmiQYQl3zs0"
#define CHAT_ID    "8773484521"

// ─── Shelf Identity ─────────────────────────────────────────
#define SHELF_ID   "Section-A Shelf-1"

// ─── Pin Definitions ────────────────────────────────────────
#define MQ135_PIN    34
#define DHT_PIN       4
#define DHT_TYPE    DHT22
#define GREEN_LED    25
#define RED_LED      26
#define BUZZER_PIN   27
#define BUTTON_PIN    32
#define MOISTURE_PIN  33
bool lastButtonState = HIGH;

// ─── DRS Thresholds ─────────────────────────────────────────
#define VOC_THRESHOLD       700
#define TEMP_THRESHOLD      28.0
#define HUMIDITY_THRESHOLD  65.0
#define LIGHT_THRESHOLD     500

// ─── DRS Alert Levels ───────────────────────────────────────
#define DRS_SAFE     3.0
#define DRS_WARNING  6

// ─── Calibration ────────────────────────────────────────────
#define K_COEFFICIENT 0.35
#define RH_BASELINE   50.0

// ─── DRS Weights ────────────────────────────────────────────
#define WEIGHT_VOC   0.40
#define WEIGHT_HUM   0.30
#define WEIGHT_TEMP  0.20
#define WEIGHT_LIGHT 0.10

// ─── Sensor Objects ─────────────────────────────────────────
DHT dht(DHT_PIN, DHT_TYPE);
BH1750 lightMeter;
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer server(80);

// ─── Global Variables ───────────────────────────────────────
float temperature   = 0;
float humidity      = 0;
float vocRaw        = 0;
float vocCalibrated = 0;
float lightLevel    = 0;
float drs           = 0;
String statusMsg    = "SAFE";
String alertMsg     = "";
String lastStatus   = "NONE"; // NONE forces first message on status change
int    lcdScreen    = 0;
bool   wifiConnected = false;
unsigned long lastSensorRead = 0;
unsigned long lastLcdSwitch  = 0;
unsigned long lastBuzzerBeep = 0;
bool buzzerState = false;
int dryBaseline = 0;
int moistureDiff = 0;

// ===== BOOK SCAN VARIABLES =====
String bookStatus = "Not Scanned";
int bookMoistureDiff = 0;
int bookVOC = 0;
float bookHumidity = 0;



unsigned long lastScanTimeMillis=0;

// ===== GRAPH HISTORY =====
#define MAX_POINTS 20

float tempHist[MAX_POINTS];
float humHist[MAX_POINTS];
float vocHist[MAX_POINTS];

int histIndex = 0;



// ─── Forward Declarations ───────────────────────────────────
void readSensors();
float calibrateVOC(float raw, float rh);
float computeDRS(float voc, float rh, float temp, float lux);
void updateAlerts();
void updateLCD();
void setupWiFi();
void setupWebServer();
void sendTelegram(String message);
String buildDashboard();


// ============================================================
//  SETUP
// ============================================================
void setup() {
  dryBaseline = analogRead(MOISTURE_PIN);
  Serial.print("Dry baseline: ");
  Serial.println(dryBaseline);


  Serial.begin(115200);
  Serial.println("\n========================================");
  Serial.println("  Library VOC Degradation Monitor");
  Serial.println("LPU Patent Project");
  Serial.println("========================================\n");

  pinMode(GREEN_LED,  OUTPUT);
  pinMode(RED_LED,    OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(GREEN_LED,  LOW);
  digitalWrite(RED_LED,    LOW);
  digitalWrite(BUZZER_PIN, LOW);
  

  // LCD init
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Library VOC Mon.");
  lcd.setCursor(0, 1); lcd.print("Initializing...");
  delay(1500);

  // DHT22
  dht.begin();
  Serial.println("[OK] DHT22 initialized");

  // BH1750
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("[OK] BH1750 initialized");
  } else {
    Serial.println("[WARN] BH1750 not found");
  }

  // WiFi
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Connecting WiFi");
  setupWiFi();

  if (wifiConnected) {
    setupWebServer();
  }

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("System Ready!");
  lcd.setCursor(0, 1); lcd.print(wifiConnected ? "WiFi: Connected" : "WiFi: Offline");
  delay(2000);
  lcd.clear();

  Serial.println("\n[READY] Monitoring started!\n");
}

void scanBook() {



  int sum = 0;
  for (int i = 0; i < 10; i++) {
     sum += analogRead(MOISTURE_PIN);
     delay(10);
  }
  int moisture = sum / 10;
// calculate difference
  moistureDiff = dryBaseline - moisture;
  int voc = analogRead(MQ135_PIN);

  bookMoistureDiff = moistureDiff;
  bookVOC = voc;
  bookHumidity = humidity;
  
  lastScanTimeMillis = millis();

  Serial.println("\n==== BOOK SCAN ====");
  Serial.print("Moisture: ");
  Serial.println(moisture);
  Serial.print("VOC: ");
  Serial.println(voc);

  lcd.clear();

 // 🔴 DAMAGED
if (moistureDiff > 40 || voc > 1200) {

  bookStatus = "DAMAGED";

  lcd.setCursor(0,0);
  lcd.print("BOOK DAMAGED");

  digitalWrite(RED_LED, HIGH);
  digitalWrite(GREEN_LED, LOW);

  digitalWrite(BUZZER_PIN, HIGH);
  delay(2000);
  digitalWrite(BUZZER_PIN, LOW);

  sendTelegram("🚨 BOOK ALERT!\nMoistureDiff: " + String(moistureDiff) +
               "\nVOC: " + String(voc));
}

// 🟡 DAMP
else if (moistureDiff > 20 || humidity > 65) {

  bookStatus = "DAMP";

  lcd.setCursor(0,0);
  lcd.print("BOOK DAMP");

  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);
}

// 🟢 SAFE
else {

  bookStatus = "SAFE";

  lcd.setCursor(0,0);
  lcd.print("BOOK SAFE");

  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);
}}
// ============================================================
//  MAIN LOOP
// ============================================================
void loop() {

  bool currentState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && currentState == LOW) {
    scanBook();
  }

  lastButtonState = currentState;

  unsigned long now = millis();

  if (now - lastSensorRead >= 5000) {
    lastSensorRead = now;

    readSensors();
    updateAlerts();

    // ✅ MOVE PRINTS HERE
    Serial.println("----------------------------------------");
    Serial.printf("Shelf ID      : %s\n",   SHELF_ID);
    Serial.printf("Temperature   : %.1f C\n", temperature);
    Serial.printf("Humidity      : %.1f %%\n", humidity);
    Serial.printf("VOC Raw       : %.0f ADC\n", vocRaw);
    Serial.printf("VOC Calibrated: %.0f ADC\n", vocCalibrated);
    Serial.printf("Light         : %.0f lux\n", lightLevel);
    Serial.printf("DRS Score     : %.2f / 10\n", drs);
    Serial.printf("Status        : %s\n", statusMsg.c_str());
    Serial.printf("Last Status   : %s\n", lastStatus.c_str());

    if (alertMsg != "") {
      Serial.printf("Alert         : %s\n", alertMsg.c_str());
    }

    Serial.println("----------------------------------------\n");
  }

  if (now - lastLcdSwitch >= 2500) {
    lastLcdSwitch = now;
    updateLCD();
    lcdScreen = (lcdScreen + 1) % 4;
  }

  if (drs > DRS_WARNING) {
    if (now - lastBuzzerBeep >= 1200) {
      lastBuzzerBeep = now;
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = false;
  }

  if (wifiConnected) {
    server.handleClient();
  }
}
// ============================================================
//  READ ALL SENSORS
// ============================================================
void readSensors() {
  // STORE HISTORY
  // ✅ SHIFT LEFT
for (int i = 0; i < MAX_POINTS - 1; i++) {
  tempHist[i] = tempHist[i + 1];
  humHist[i]  = humHist[i + 1];
  vocHist[i]  = vocHist[i + 1];
}

// ✅ ADD NEW VALUE AT END
  tempHist[MAX_POINTS - 1] = temperature;
  humHist[MAX_POINTS - 1]  = humidity;
  vocHist[MAX_POINTS - 1]  = vocCalibrated;


  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperature = t;
  if (!isnan(h)) humidity    = h;

  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(MQ135_PIN);
    delay(5);
  }
  vocRaw = sum / 10.0;
  vocCalibrated = calibrateVOC(vocRaw, humidity);

  float lux = lightMeter.readLightLevel();
  if (lux >= 0) lightLevel = lux;

  drs = computeDRS(vocCalibrated, humidity, temperature, lightLevel);
}

// ============================================================
//  NOVEL CONTRIBUTION 1: Cross-Sensitivity Calibration
// ============================================================
float calibrateVOC(float raw, float rh) {
  float correction = 1.0 + K_COEFFICIENT * (rh - RH_BASELINE) / 100.0;
  if (correction <= 0) correction = 0.01;
  return raw / correction;
}

// ============================================================
//  NOVEL CONTRIBUTION 2: Weighted DRS Algorithm
// ============================================================
float computeDRS(float voc, float rh, float temp, float lux) {
  float fVOC   = constrain((voc  - VOC_THRESHOLD)       / (float)VOC_THRESHOLD,         0.0, 1.0);
  float fHum   = constrain((rh   - HUMIDITY_THRESHOLD)  / (100.0 - HUMIDITY_THRESHOLD),  0.0, 1.0);
  float fTemp  = constrain((temp - TEMP_THRESHOLD)      / (45.0  - TEMP_THRESHOLD),      0.0, 1.0);
  float fLight = constrain((lux  - LIGHT_THRESHOLD)     / (2000.0 - LIGHT_THRESHOLD),    0.0, 1.0);

  float score = (WEIGHT_VOC   * fVOC)
              + (WEIGHT_HUM   * fHum)
              + (WEIGHT_TEMP  * fTemp)
              + (WEIGHT_LIGHT * fLight);

  return score * 10.0;
}

// ============================================================
//  SEND TELEGRAM MESSAGE
// ============================================================
void sendTelegram(String message) {
  if (!wifiConnected) {
    Serial.println("[Telegram] No WiFi - skipping");
    return;
  }

  Serial.println("[Telegram] Sending message...");

  WiFiClientSecure client;
  client.setInsecure(); // Skip SSL certificate verification

  HTTPClient http;
  String url = "https://api.telegram.org/bot";
  url += BOT_TOKEN;
  url += "/sendMessage";

  http.begin(client, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "chat_id=";
  postData += CHAT_ID;
  postData += "&text=";
  postData += message;

  int httpCode = http.POST(postData);

  Serial.print("[Telegram] HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode == 200) {
    Serial.println("[Telegram] Message sent successfully!");
  } else {
    Serial.print("[Telegram] Failed! Response: ");
    Serial.println(http.getString());
  }

  http.end();
}

// ============================================================
//  UPDATE ALERTS — LEDs, BUZZER, TELEGRAM
// ============================================================
void updateAlerts() {
  alertMsg = "";
  String currentStatus = "";

  if (drs <= DRS_SAFE) {
    currentStatus = "SAFE";
    statusMsg = "SAFE";
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED,   LOW);

  } else if (drs <= DRS_WARNING) {
    currentStatus = "WARNING";
    statusMsg = "WARNING";
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED,   HIGH);
    if (vocCalibrated > VOC_THRESHOLD)  alertMsg += "High VOC! ";
    if (humidity > HUMIDITY_THRESHOLD)  alertMsg += "High Humidity! ";
    if (temperature > TEMP_THRESHOLD)   alertMsg += "High Temp! ";
    if (lightLevel > LIGHT_THRESHOLD)   alertMsg += "Excess Light! ";

  } else {
    currentStatus = "CRITICAL";
    statusMsg = "CRITICAL";
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED,   HIGH);
    alertMsg = "URGENT: Preservation Required!";
  }

  // Send Telegram ONLY when status changes — and ONLY for WARNING/CRITICAL
  //buzzer??? 
  if (currentStatus != lastStatus) {
    Serial.println("[Status Change] " + lastStatus + " -> " + currentStatus);

    if (currentStatus == "WARNING") {
      String msg = "WARNING - Library VOC Monitor";
      msg += "%0ADRS Score: " + String(drs, 1) + "/10";
      msg += "%0AAlert: " + alertMsg;
      msg += "%0ATemp: " + String(temperature, 1) + "C";
      msg += "%0AHumidity: " + String(humidity, 1) + "%25";
      msg += "%0AVOC: " + String((int)vocCalibrated) + " ADC";
      msg += "%0AShelf: " + String(SHELF_ID);
      sendTelegram(msg);

    } else if (currentStatus == "CRITICAL") {
      String msg = "CRITICAL ALERT - Library VOC Monitor";
      msg += "%0ADRS Score: " + String(drs, 1) + "/10";
      msg += "%0A" + alertMsg;
      msg += "%0ATemp: " + String(temperature, 1) + "C";
      msg += "%0AHumidity: " + String(humidity, 1) + "%25";
      msg += "%0AVOC: " + String((int)vocCalibrated) + " ADC";
      msg += "%0AShelf: " + String(SHELF_ID);
      sendTelegram(msg);


    }
    // No message for SAFE — librarian doesn't need to know when it's safe
    lastStatus = currentStatus;
  }
}

// ============================================================
//  LCD DISPLAY — 4 Rotating Screens
// ============================================================
void updateLCD() {
  lcd.clear();
  switch (lcdScreen) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("Temp:");
      lcd.print(temperature, 1);
      lcd.print((char)223);
      lcd.print("C");
      lcd.setCursor(0, 1);
      lcd.print("Humidity:");
      lcd.print(humidity, 1);
      lcd.print("%");
      break;

    case 1:
      lcd.setCursor(0, 0);
      lcd.print("VOC Raw:");
      lcd.print((int)vocRaw);
      lcd.setCursor(0, 1);
      lcd.print("VOC Cal:");
      lcd.print((int)vocCalibrated);
      break;

    case 2:
      lcd.setCursor(0, 0);
      lcd.print("Light:");
      lcd.print((int)lightLevel);
      lcd.print(" lux");
      lcd.setCursor(0, 1);
      lcd.print(lightLevel > LIGHT_THRESHOLD ? "! Above Safe  " : "  Within Safe ");
      break;

    case 3:
      lcd.setCursor(0, 0);
      lcd.print("DRS:");
      lcd.print(drs, 1);
      lcd.print("/10  ");
      lcd.setCursor(0, 1);
      if (statusMsg == "SAFE")          lcd.print("Status: SAFE    ");
      else if (statusMsg == "WARNING")  lcd.print("Status: WARNING!");
      else                              lcd.print("!! CRITICAL !!  ");
      break;
  }
}

// ============================================================
//  WiFi SETUP
// ============================================================
void setupWiFi() {
  WiFi.disconnect(true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  delay(1000);

  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n[WiFi] Connected!");
    Serial.print("[WiFi] IP Address: http://");
    Serial.println(WiFi.localIP());
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    wifiConnected = false;
    Serial.println("\n[WiFi] Failed - running offline");
  }
}

// ============================================================
//  WEB SERVER SETUP
// ============================================================
void setupWebServer() {
  server.on("/", []() {
    server.send(200, "text/html", buildDashboard());
  });
  server.on("/book", []() {
  server.send(200, "text/html", buildBookPage());
  });

  server.on("/scan", []() {
  scanBook();
  server.sendHeader("Location", "/book");
  server.send(303);
  });

  server.on("/graph", []() {

  String json = "{";

  json += "\"temp\":[";
  for (int i = 0; i < MAX_POINTS; i++) {
    json += String(tempHist[i]);
    if (i < MAX_POINTS - 1) json += ",";
  }
  json += "],";

  json += "\"hum\":[";
  for (int i = 0; i < MAX_POINTS; i++) {
    json += String(humHist[i]);
    if (i < MAX_POINTS - 1) json += ",";
  }
  json += "],";

  json += "\"voc\":[";
  for (int i = 0; i < MAX_POINTS; i++) {
    json += String(vocHist[i]);
    if (i < MAX_POINTS - 1) json += ",";
  }
  json += "]";

  json += "}";

  server.send(200, "application/json", json);
  });

  server.on("/data", []() {
    String json = "{";
    json += "\"shelf\":\"" + String(SHELF_ID) + "\",";
    json += "\"temperature\":" + String(temperature, 1) + ",";
    json += "\"humidity\":" + String(humidity, 1) + ",";
    json += "\"voc_raw\":" + String(vocRaw, 0) + ",";
    json += "\"voc_calibrated\":" + String(vocCalibrated, 0) + ",";
    json += "\"light_lux\":" + String(lightLevel, 0) + ",";
    json += "\"drs\":" + String(drs, 2) + ",";
    json += "\"status\":\"" + statusMsg + "\",";
    json += "\"alert\":\"" + alertMsg + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  server.begin();
  Serial.println("[WebServer] Dashboard at http://" + WiFi.localIP().toString());
}

// ============================================================
//  HTML DASHBOARD
// ============================================================
String buildDashboard() {

  // ── status-dependent values ─────────────────────────────
  String statusPale, statusBar, statusText, statusEmoji, headline, sub;
  if (statusMsg == "SAFE") {
    statusPale  = "#e8f5e8"; statusBar  = "#5a8a5a"; statusText = "#2d6a2d";
    statusEmoji = "&#128215;";  // book emoji fallback
    headline = "Your books are doing well";
    sub      = "All shelf conditions are within safe preservation limits. No action needed right now.";
  } else if (statusMsg == "WARNING") {
    statusPale  = "#fef9e7"; statusBar  = "#e8b84b"; statusText = "#8a6200";
    statusEmoji = "&#128217;";
    headline = "Attention needed";
    sub      = "One or more shelf conditions are outside the recommended preservation range. Check the readings below.";
  } else {
    statusPale  = "#fdeaea"; statusBar  = "#c0392b"; statusText = "#922b21";
    statusEmoji = "&#128216;";
    headline = "Immediate action required";
    sub      = "Preservation conditions are critical. Please inspect this shelf and take corrective action now.";
  }

  int drsPct = (int)(drs / 10.0 * 100.0);

  // ── per-sensor pill class + label ──────────────────────
  auto pillClass = [](float val, float thresh, String status) -> String {
    if (val > thresh) return (status == "CRITICAL") ? "pill-crit" : "pill-warn";
    return "pill-safe";
  };
  auto pillLabel = [](float val, float thresh) -> String {
    return (val > thresh) ? "Above safe range" : "Safe";
  };

  String tPill  = pillClass(temperature,  TEMP_THRESHOLD,     statusMsg);
  String hPill  = pillClass(humidity,     HUMIDITY_THRESHOLD, statusMsg);
  String vPill  = pillClass(vocCalibrated,VOC_THRESHOLD,      statusMsg);
  String lPill  = pillClass(lightLevel,   LIGHT_THRESHOLD,    statusMsg);

  String tLabel = pillLabel(temperature,   TEMP_THRESHOLD);
  String hLabel = pillLabel(humidity,      HUMIDITY_THRESHOLD);
  String vLabel = pillLabel(vocCalibrated, VOC_THRESHOLD);
  String lLabel = pillLabel(lightLevel,    LIGHT_THRESHOLD);

  // ── calibration delta text ──────────────────────────────
  int calDelta = (int)abs(vocRaw - vocCalibrated);
  String calText = (calDelta > 0)
    ? (String(calDelta) + " ADC units removed by humidity correction at " + String((int)humidity) + "% RH")
    : ("No correction needed at " + String((int)humidity) + "% RH");

  // ── alert banner (only WARNING/CRITICAL) ────────────────
  String alertHTML = "";
  if (statusMsg != "SAFE" && alertMsg != "") {
    String abg = (statusMsg == "CRITICAL") ? "#fdeaea" : "#fff8e6";
    String aborder = (statusMsg == "CRITICAL") ? "#e0a0a0" : "#f0d080";
    String aicon   = (statusMsg == "CRITICAL") ? "&#128680;" : "&#9888;&#65039;";
    String atextc  = (statusMsg == "CRITICAL") ? "#922b21" : "#7a5c00";
    String asubc   = (statusMsg == "CRITICAL") ? "#c0392b" : "#a08030";
    alertHTML = "<div class='alert-banner' style='background:" + abg + ";border-color:" + aborder + "'>";
    alertHTML += "<div class='alert-icon'>" + aicon + "</div>";
    alertHTML += "<div><div class='alert-text-main' style='color:" + atextc + "'>" + alertMsg + "</div>";
    alertHTML += "<div class='alert-text-sub' style='color:" + asubc + "'>Check the sensor readings below and take corrective action.</div></div>";
    alertHTML += "</div>";
  }

  String h = "";

  h += "<!DOCTYPE html><html lang='en'><head>";
  h += "<meta charset='UTF-8'>";
  h += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<meta http-equiv='refresh' content='5'>";
  h += "<title>Library Health Monitor</title>";
  h += "<link href='https://fonts.googleapis.com/css2?family=Nunito:wght@400;600;700;800&family=Nunito+Sans:wght@400;600&display=swap' rel='stylesheet'>";
  h += "<style>";
  h += "*{box-sizing:border-box;margin:0;padding:0}";
  h += "body{font-family:'Nunito Sans',sans-serif;background:#f5f0e8;min-height:100vh;color:#2d2a24}";
  h += ".page{max-width:680px;margin:0 auto;padding:28px 20px 60px}";
  h += ".topbar{display:flex;justify-content:space-between;align-items:center;margin-bottom:28px}";
  h += ".topbar-left{font-family:'Nunito',sans-serif;font-size:13px;font-weight:700;color:#7c6f5a}";
  h += ".live-dot{display:inline-flex;align-items:center;gap:6px;font-size:12px;font-weight:600;color:#5a8a5a;background:#e2f0e2;padding:5px 12px;border-radius:100px}";
  h += ".live-dot::before{content:'';width:7px;height:7px;border-radius:50%;background:#5a8a5a;animation:blink 1.8s ease-in-out infinite}";
  h += "@keyframes blink{0%,100%{opacity:1}50%{opacity:0.3}}";
  h += ".alert-banner{border-width:1.5px;border-style:solid;border-radius:16px;padding:16px 20px;display:flex;align-items:flex-start;gap:12px;margin-bottom:16px}";
  h += ".alert-icon{font-size:20px;flex-shrink:0;margin-top:1px}";
  h += ".alert-text-main{font-family:'Nunito',sans-serif;font-size:14px;font-weight:700;margin-bottom:3px}";
  h += ".alert-text-sub{font-size:12px}";
  h += ".greeting-card{background:#fff;border-radius:24px;padding:28px 28px 24px;margin-bottom:18px;border:1.5px solid #e8e0d0;position:relative;overflow:hidden}";

  h += ".greeting-card::after{content:'';position:absolute;top:-40px;right:-40px;width:140px;height:140px;border-radius:50%;background:";
  h += statusPale + ";pointer-events:none}";

  h += ".shelf-tag{display:inline-block;background:#f0ebe0;color:#7c6f5a;font-size:11px;font-weight:700;padding:4px 12px;border-radius:100px;margin-bottom:14px;letter-spacing:.06em;text-transform:uppercase}";
  h += ".status-emoji{font-size:42px;line-height:1;display:block;margin-bottom:10px}";
  h += ".greeting-headline{font-family:'Nunito',sans-serif;font-size:24px;font-weight:800;margin-bottom:6px;line-height:1.15;color:" + statusText + "}";
  h += ".greeting-sub{font-size:14px;color:#8a8070;line-height:1.5;max-width:420px}";
  h += ".score-section{margin-top:22px;padding-top:18px;border-top:1.5px dashed #e8e0d0}";
  h += ".score-label-row{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:10px}";
  h += ".score-label{font-size:12px;font-weight:700;color:#7c6f5a;text-transform:uppercase;letter-spacing:.06em}";
  h += ".score-number{font-family:'Nunito',sans-serif;font-size:28px;font-weight:800;color:" + statusText + "}";
  h += ".score-number span{font-size:14px;font-weight:600;color:#aaa09a}";
  h += ".bar-wrap{background:#f0ebe0;border-radius:100px;height:14px;overflow:hidden}";
  h += ".bar-fill{height:100%;border-radius:100px;background:" + statusBar + ";width:" + String(drsPct) + "%;transition:width .8s ease}";
  h += ".bar-labels{display:flex;justify-content:space-between;margin-top:6px;font-size:10px;color:#b0a898;font-weight:600}";
  h += ".section-title{font-family:'Nunito',sans-serif;font-size:13px;font-weight:800;color:#7c6f5a;text-transform:uppercase;letter-spacing:.08em;margin:24px 0 12px}";
  h += ".sensor-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}";
  h += "@media(max-width:480px){.sensor-grid{grid-template-columns:1fr}}";
  h += ".sensor-card{background:#fff;border-radius:18px;padding:20px;border:1.5px solid #e8e0d0;transition:transform .15s ease,box-shadow .15s ease;cursor:default}";
  h += ".sensor-card:hover{transform:translateY(-2px);box-shadow:0 8px 24px rgba(0,0,0,.07)}";
  h += ".sensor-icon{font-size:24px;display:block;margin-bottom:10px}";
  h += ".sensor-name{font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.07em;color:#a09888;margin-bottom:4px}";
  h += ".sensor-value{font-family:'Nunito',sans-serif;font-size:32px;font-weight:800;line-height:1;margin-bottom:2px}";
  h += ".sensor-unit{font-size:11px;color:#b0a898;font-weight:600}";
  h += ".sensor-status-pill{display:inline-block;font-size:10px;font-weight:700;padding:3px 9px;border-radius:100px;margin-top:10px;text-transform:uppercase;letter-spacing:.05em}";
  h += ".pill-safe{background:#e2f0e2;color:#3d7a3d}";
  h += ".pill-warn{background:#fef3cd;color:#9a7200}";
  h += ".pill-crit{background:#fde8e8;color:#c0392b}";
  h += ".cal-card{background:#fff;border-radius:18px;padding:22px;border:1.5px solid #e8e0d0;margin-bottom:12px}";
  h += ".cal-top{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;gap:12px}";
  h += ".cal-heading{font-family:'Nunito',sans-serif;font-size:14px;font-weight:800;color:#2d2a24}";
  h += ".cal-sub{font-size:11px;color:#a09888;margin-top:2px}";
  h += ".cal-badge{background:#f0ebe0;color:#7c6f5a;font-size:10px;font-weight:700;padding:4px 10px;border-radius:100px;white-space:nowrap;flex-shrink:0}";
  h += ".cal-numbers{display:flex;align-items:center;gap:16px;padding:16px;background:#f9f6f0;border-radius:12px}";
  h += ".cal-num-box{flex:1;text-align:center}";
  h += ".cal-num-label{font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.07em;color:#a09888;margin-bottom:4px}";
  h += ".cal-num-val{font-family:'Nunito',sans-serif;font-size:24px;font-weight:800;color:#2d2a24}";
  h += ".cal-arrow-box{color:#c0b8a8;font-size:20px;flex-shrink:0}";
  h += ".cal-result{font-family:'Nunito',sans-serif;font-size:24px;font-weight:800;color:#5a8a5a}";
  h += ".cal-formula{font-family:monospace;font-size:11px;color:#b0a898;margin-top:12px;text-align:center}";
  h += ".footer{margin-top:36px;text-align:center;font-size:11px;color:#b0a898;font-weight:600;line-height:1.8}";
  h += ".footer a{color:#8a7a6a;text-decoration:none;border-bottom:1px dashed #c0b0a0}";
  h += "</style></head><body>";

  h += "<div class='page'>";

  // topbar
  h += "<div class='topbar'>";
  h += "<div style='margin-bottom:15px'>";
  h += "<a href='/' style='margin-right:15px;font-weight:bold'>📊 Dashboard</a>";
  h += "<a href='/book' style='font-weight:bold'>📘 Book Scan</a>";
  h += "</div>";

  h += "<div class='topbar-left'>&#128218; Library VOC Monitor</div>";
  h += "<div class='live-dot'>Live</div>";
  h += "</div>";

  // alert banner
  h += alertHTML;

  // greeting card
  h += "<div class='greeting-card'>";
  h += "<div class='shelf-tag'>" + String(SHELF_ID) + "</div>";
  h += "<div class='status-emoji'>" + statusEmoji + "</div>";
  h += "<div class='greeting-headline'>" + headline + "</div>";
  h += "<div class='greeting-sub'>" + sub + "</div>";
  h += "<div class='score-section'>";
  h += "<div class='score-label-row'>";
  h += "<div class='score-label'>Degradation Risk Score</div>";
  h += "<div class='score-number'>" + String(drs, 1) + " <span>/ 10</span></div>";
  h += "</div>";
  h += "<div class='bar-wrap'><div class='bar-fill'></div></div>";
  h += "<div class='bar-labels'><span>0 Safe</span><span>4 Warning</span><span>7 Critical</span><span>10</span></div>";
  h += "</div></div>";

  // sensor section title
  h += "<div class='section-title'>What the sensors are reading</div>";
  h += "<div class='sensor-grid'>";

  // Temperature
  String tColor = (temperature > TEMP_THRESHOLD) ? "#c0392b" : "#2d6a2d";
  h += "<div class='sensor-card'>";
  h += "<span class='sensor-icon'>&#127777;&#65039;</span>";
  h += "<div class='sensor-name'>Temperature</div>";
  h += "<div class='sensor-value' style='color:" + tColor + "'>" + String(temperature, 1) + "</div>";
  h += "<div class='sensor-unit'>&deg;C &nbsp;&middot;&nbsp; safe below " + String((int)TEMP_THRESHOLD) + "&deg;C</div>";
  h += "<span class='sensor-status-pill " + tPill + "'>" + tLabel + "</span></div>";

  // Humidity
  String hColor = (humidity > HUMIDITY_THRESHOLD) ? "#c0392b" : "#3d7a9a";
  h += "<div class='sensor-card'>";
  h += "<span class='sensor-icon'>&#128167;</span>";
  h += "<div class='sensor-name'>Humidity</div>";
  h += "<div class='sensor-value' style='color:" + hColor + "'>" + String(humidity, 1) + "</div>";
  h += "<div class='sensor-unit'>% RH &nbsp;&middot;&nbsp; safe below " + String((int)HUMIDITY_THRESHOLD) + "%</div>";
  h += "<span class='sensor-status-pill " + hPill + "'>" + hLabel + "</span></div>";

  // VOC
  String vColor = (vocCalibrated > VOC_THRESHOLD) ? "#c0392b" : "#7a5a2a";
  h += "<div class='sensor-card'>";
  h += "<span class='sensor-icon'>&#129371;</span>";
  h += "<div class='sensor-name'>VOC Gas (calibrated)</div>";
  h += "<div class='sensor-value' style='color:" + vColor + "'>" + String((int)vocCalibrated) + "</div>";
  h += "<div class='sensor-unit'>ADC &nbsp;&middot;&nbsp; safe below " + String(VOC_THRESHOLD) + "</div>";
  h += "<span class='sensor-status-pill " + vPill + "'>" + vLabel + "</span></div>";

  // Light
  String lColor = (lightLevel > LIGHT_THRESHOLD) ? "#c0392b" : "#7a7a2a";
  h += "<div class='sensor-card'>";
  h += "<span class='sensor-icon'>&#9728;&#65039;</span>";
  h += "<div class='sensor-name'>Light Intensity</div>";
  h += "<div class='sensor-value' style='color:" + lColor + "'>" + String((int)lightLevel) + "</div>";
  h += "<div class='sensor-unit'>lux &nbsp;&middot;&nbsp; safe below " + String(LIGHT_THRESHOLD) + " lux</div>";
  h += "<span class='sensor-status-pill " + lPill + "'>" + lLabel + "</span></div>";

  h += "</div>"; // sensor-grid

  h += "<div class='section-title'>Sensor Trends</div>";
  h += "<canvas id='chart' height='200'></canvas>";

  h += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";

  h += "<script>";
  h += "let chart;";
  h += "async function loadChart(){";
  h += "let res = await fetch('/graph');";
  h += "let data = await res.json();";

  h += "chart = new Chart(document.getElementById('chart'), {";
  h += "type:'line',";
  h += "data:{labels:Array(data.temp.length).fill(''),datasets:[";
  h += "{label:'Temp',data:data.temp},";
  h += "{label:'Humidity',data:data.hum},";
  h += "{label:'VOC',data:data.voc}";
  h += "]}});";
  h += "}";

  h += "async function updateChart(){";
  h += "let res = await fetch('/graph');";
  h += "let data = await res.json();";

  h += "chart.data.datasets[0].data = data.temp;";
  h += "chart.data.datasets[1].data = data.hum;";
  h += "chart.data.datasets[2].data = data.voc;";
  h += "chart.update();";
  h += "}";

  h += "loadChart();";
  h += "setInterval(updateChart, 3000);";
  h += "</script>";

// ===== PRO INSIGHTS PANEL =====
h += "<div class='section-title'>System Insights</div>";
h += "<div class='cal-card'>";

// LOGIC
int vocLevel = vocCalibrated;
int humLevel = humidity;

int vocPercent = min(100, max(0, (int)map(vocLevel, 300, 1500, 0, 100)));
int humPercent = min(100, max(0, (int)map(humLevel, 20, 90, 0, 100)));

String overall = "SAFE";
String overallColor = "#2d6a2d";

if (vocLevel > VOC_THRESHOLD || humLevel > 65) {
  overall = "WARNING";
  overallColor = "#e8b84b";
}
if (vocLevel > VOC_THRESHOLD + 200) {
  overall = "DANGER";
  overallColor = "#c0392b";
}

// HEADER STATUS
h += "<div style='font-size:28px;font-weight:900;margin-bottom:12px;color:" + overallColor + ";letter-spacing:1px'>";
h += overall;
h += "</div>";

// VOC BAR
h += "<div style='margin-bottom:12px'>";
h += "<div style='font-size:12px;color:#8a8070'>🔥 VOC Level</div>";
h += "<div style='background:#eee;border-radius:8px;height:10px;overflow:hidden'>";
h += "<div style='width:" + String(vocPercent) + "%;background:#c0392b;height:100%;transition:width 0.8s ease'></div>";
h += "</div>";
h += "<div style='font-size:13px;margin-top:4px'>" + String(vocLevel) + "</div>";
h += "</div>";

// HUMIDITY BAR
h += "<div style='margin-bottom:12px'>";
h += "<div style='font-size:12px;color:#8a8070'>💧 Humidity</div>";
h += "<div style='background:#eee;border-radius:8px;height:10px;overflow:hidden'>";
h += "<div style='width:" + String(humPercent) + "%;background:#3498db;height:100%'></div>";
h += "</div>";
h += "<div style='font-size:13px;margin-top:4px'>" + String(humLevel) + "%</div>";
h += "</div>";

// FOOT TEXT
h += "<div style='margin-top:10px;font-size:13px;color:#8a8070'>";
h += "Live environmental monitoring";
h += "</div>";

h += "</div>";

  // calibration card
  h += "<div class='section-title'>How we corrected the gas reading</div>";
 String bg = "#ffffff";
if (overall == "WARNING") bg = "#fff8e6";
if (overall == "DANGER") bg = "#fdeaea";

h += "<div class='cal-card' style='background:" + bg + "'>";
  h += "<div class='cal-top'>";
  h += "<div><div class='cal-heading'>Humidity cross-sensitivity correction</div>";
  h += "<div class='cal-sub'>Raw MQ-135 reading adjusted for ambient humidity</div></div>";
  h += "<div class='cal-badge'>Novel Contribution I</div></div>";
  h += "<div class='cal-numbers'>";
  h += "<div class='cal-num-box'><div class='cal-num-label'>Raw sensor</div>";
  h += "<div class='cal-num-val'>" + String((int)vocRaw) + "</div>";
  h += "<div class='cal-num-label' style='margin-top:2px'>ADC</div></div>";
  h += "<div class='cal-arrow-box'>&rarr;</div>";
  h += "<div class='cal-num-box'><div class='cal-num-label'>After correction</div>";
  h += "<div class='cal-result'>" + String((int)vocCalibrated) + "</div>";
  h += "<div class='cal-num-label' style='margin-top:2px;color:#5a8a5a'>ADC</div></div>";
  h += "</div>";
  h += "<div class='cal-formula'>" + calText + "</div>";
  h += "<div class='cal-formula'>VOC-cal = VOC-raw &divide; ( 1 + 0.35 &times; ( RH &minus; 50 ) / 100 )</div>";
  h += "</div>";

  // footer
  h += "<div class='footer'>";
  h += "Refreshes every 5 seconds &nbsp;&middot;&nbsp; <a href='/data'>View raw JSON</a><br>";
  
  h += "</div>";

  h += "</div></body></html>";
  return h;
}



String buildBookPage() {

  String h = "";

  h += "<!DOCTYPE html><html><head>";
  h += "<meta charset='UTF-8'>";

  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<meta http-equiv='refresh' content='3'>";
  h += "<title>Book Scan</title>";

  h += "<style>";
  h += "body{font-family:Arial;background:#f5f0e8;padding:20px}";
  h += ".card{background:white;padding:20px;border-radius:15px}";
  h += ".btn{background:#5a8a5a;color:white;padding:10px 15px;border-radius:8px;text-decoration:none;font-weight:bold}";
  h += "</style></head><body>";

  // TITLE
  h += "<h2>📘 Book Scan</h2>";

  // SCAN BUTTON
  h += "<a class='btn' href='/scan'>🔍 Scan Book</a><br><br>";

  // DATA CARD
  h += "<div class='card'>";
  h += "<p><b>Status:</b> " + bookStatus + "</p>";
  h += "<p><b>Moisture Diff:</b> " + String(bookMoistureDiff) + "</p>";
  h += "<p><b>VOC:</b> " + String(bookVOC) + "</p>";
  h += "<p><b>Humidity:</b> " + String(bookHumidity) + "%</p>";

  h += "<p><b>Humidity:</b> " + String(bookHumidity) + "%</p>";

  unsigned long elapsed = (millis() - lastScanTimeMillis) / 1000;

  String lastScanTime;
  if (elapsed < 2)
    lastScanTime = "Just now";
  else
    lastScanTime = String(elapsed) + " sec ago";

  h += "<p><b>Last Scan:</b> " + lastScanTime + "</p>";

  h += "<p><b>Last Scan:</b> " + lastScanTime + "</p>";
  h += "</div>";

  // BACK BUTTON
  h += "<br><a href='/'>⬅ Back to Dashboard</a>";

  h += "</body></html>";

  return h;
}