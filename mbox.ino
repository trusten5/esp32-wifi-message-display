#include <SPI.h>
#include <Wire.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

// ----- OLED -----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ----- RFID -----
#define RST_PIN  1
#define SS_PIN   42

// ----- Adafruit IO -----
#define AIO_SERVER "io.adafruit.com"
#define AIO_PORT   1883

// ----- State -----
String currentMessage = "";
bool newMessage = false;
unsigned long lastDisplaySwitch = 0;
unsigned long lastWeatherFetch = 0;
int currentScreen = 0; // 0=clock, 1=weather, 2=distance, 3=screensaver
#define DISPLAY_SWITCH_INTERVAL 30000
#define WEATHER_FETCH_INTERVAL  600000
unsigned long locationReceivedAt = 0;
#define LOCATION_MAX_AGE 86400000

// ----- Weather data -----
float weatherTemp = 0;
int weatherHumidity = 0;
String weatherDesc = "";
int weatherId = 0;

// ----- Location -----
float myLat = 0;
float myLon = 0;
bool locationKnown = false;

// ----- Screensaver -----
int ssX = 10;
int ssDir = 1;
int ssFrame = 0;
unsigned long ssLastUpdate = 0;

MFRC522 mfrc522(SS_PIN, RST_PIN);
WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_PORT, AIO_USERNAME, AIO_KEY);
Adafruit_MQTT_Subscribe mbox     = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/" AIO_FEED);
Adafruit_MQTT_Subscribe location = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/location");

// =====================
// HELPERS
// =====================

bool uidMatches(byte *uid, byte *known, byte size) {
  for (byte i = 0; i < size; i++) {
    if (uid[i] != known[i]) return false;
  }
  return true;
}

float toRad(float deg) { return deg * PI / 180.0; }

float calcDistance(float lat1, float lon1, float lat2, float lon2) {
  float R = 3958.8;
  float dLat = toRad(lat2 - lat1);
  float dLon = toRad(lon2 - lon1);
  float a = sin(dLat/2)*sin(dLat/2) +
            cos(toRad(lat1))*cos(toRad(lat2))*
            sin(dLon/2)*sin(dLon/2);
  float c = 2 * atan2(sqrt(a), sqrt(1-a));
  return R * c;
}

// =====================
// CONNECT
// =====================

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected!");
}

void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to Adafruit IO...");
    int8_t ret = mqtt.connect();
    if (ret == 0) {
      Serial.println(" connected!");
    } else {
      Serial.print(" failed, retrying in 3s: ");
      Serial.println(mqtt.connectErrorString(ret));
      delay(3000);
    }
  }
}

// =====================
// WEATHER
// =====================

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?id="
               + String(WEATHER_CITY_ID) + "&appid="
               + String(WEATHER_API_KEY) + "&units=imperial";
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    deserializeJson(doc, payload);
    weatherTemp     = doc["main"]["temp"];
    weatherHumidity = doc["main"]["humidity"];
    weatherDesc     = doc["weather"][0]["description"].as<String>();
    weatherId       = doc["weather"][0]["id"];
    weatherDesc[0]  = toupper(weatherDesc[0]);
  }
  http.end();
}

// =====================
// WEATHER ICONS
// =====================

void drawSun(int x, int y) {
  display.fillCircle(x, y, 8, SSD1306_WHITE);
  for (int i = 0; i < 8; i++) {
    float angle = i * PI / 4;
    display.drawLine(x + cos(angle)*11, y + sin(angle)*11,
                     x + cos(angle)*15, y + sin(angle)*15, SSD1306_WHITE);
  }
}

void drawCloud(int x, int y) {
  display.fillCircle(x,    y,   8, SSD1306_WHITE);
  display.fillCircle(x+10, y-3, 6, SSD1306_WHITE);
  display.fillCircle(x+18, y,   7, SSD1306_WHITE);
  display.fillRect(x-8, y, 35, 10, SSD1306_WHITE);
}

void drawRain(int x, int y) {
  drawCloud(x, y);
  for (int i = 0; i < 4; i++)
    display.drawLine(x + i*8, y+12, x + i*8 - 2, y+18, SSD1306_WHITE);
}

void drawSnow(int x, int y) {
  drawCloud(x, y);
  for (int i = 0; i < 4; i++)
    display.fillCircle(x + i*8, y+16, 2, SSD1306_WHITE);
}

void drawThunder(int x, int y) {
  drawCloud(x, y);
  display.drawLine(x+10, y+12, x+6,  y+20, SSD1306_WHITE);
  display.drawLine(x+6,  y+20, x+12, y+20, SSD1306_WHITE);
  display.drawLine(x+12, y+20, x+8,  y+28, SSD1306_WHITE);
}

void drawMist(int x, int y) {
  for (int i = 0; i < 4; i++)
    display.drawFastHLine(x, y + i*5, 30, SSD1306_WHITE);
}

void drawWeatherIcon(int x, int y) {
  if      (weatherId == 800)                     drawSun(x, y);
  else if (weatherId >= 801 && weatherId <= 804) drawCloud(x, y);
  else if (weatherId >= 500 && weatherId < 600)  drawRain(x, y);
  else if (weatherId >= 300 && weatherId < 400)  drawRain(x, y);
  else if (weatherId >= 600 && weatherId < 700)  drawSnow(x, y);
  else if (weatherId >= 200 && weatherId < 300)  drawThunder(x, y);
  else if (weatherId >= 700 && weatherId < 800)  drawMist(x, y);
  else                                           drawSun(x, y);
}

// =====================
// SCREENSAVER
// =====================

void drawSprite(int x, int frame) {
  // Simple walking pixel-art sprite
  display.fillRect(x,    26, 28, 6,  SSD1306_WHITE);
  display.fillRect(x+24, 23, 5,  5,  SSD1306_WHITE);
  display.fillRect(x+26, 17, 10, 9,  SSD1306_WHITE);
  display.fillRect(x+34, 20, 5,  5,  SSD1306_WHITE);
  display.fillRect(x+26, 16, 5,  8,  SSD1306_WHITE);
  display.fillRect(x+30, 19, 2,  2,  SSD1306_BLACK);
  display.fillRect(x+37, 22, 1,  1,  SSD1306_BLACK);

  if (frame % 2 == 0) {
    display.fillRect(x-3, 22, 3, 6, SSD1306_WHITE);
    display.fillRect(x-5, 19, 3, 4, SSD1306_WHITE);
  } else {
    display.fillRect(x-3, 24, 3, 6, SSD1306_WHITE);
    display.fillRect(x-3, 30, 4, 3, SSD1306_WHITE);
  }

  if (frame % 2 == 0) {
    display.fillRect(x+4,  32, 3,  8, SSD1306_WHITE);
    display.fillRect(x+11, 29, 3, 11, SSD1306_WHITE);
    display.fillRect(x+18, 32, 3,  8, SSD1306_WHITE);
    display.fillRect(x+24, 29, 3, 11, SSD1306_WHITE);
  } else {
    display.fillRect(x+4,  29, 3, 11, SSD1306_WHITE);
    display.fillRect(x+11, 32, 3,  8, SSD1306_WHITE);
    display.fillRect(x+18, 29, 3, 11, SSD1306_WHITE);
    display.fillRect(x+24, 32, 3,  8, SSD1306_WHITE);
  }
  display.fillRect(x+3,  39, 5, 2, SSD1306_WHITE);
  display.fillRect(x+10, 39, 5, 2, SSD1306_WHITE);
  display.fillRect(x+17, 39, 5, 2, SSD1306_WHITE);
  display.fillRect(x+23, 39, 5, 2, SSD1306_WHITE);
}

void showScreensaver() {
  unsigned long now = millis();
  if (now - ssLastUpdate > 180) {
    ssLastUpdate = now;
    ssX += ssDir * 2;
    ssFrame++;
    if (ssX > 82) ssDir = -1;
    if (ssX < 5)  ssDir = 1;
  }
  display.clearDisplay();
  drawSprite(ssX, ssFrame);
  display.display();
}

// =====================
// DISPLAY SCREENS
// =====================

void showWeather() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(40, 0);
  display.println("Weather");
  drawWeatherIcon(88, 28);
  display.setTextSize(2);
  display.setCursor(0, 14);
  display.print((int)weatherTemp);
  display.print((char)247);
  display.println("F");
  display.setTextSize(1);
  display.setCursor(0, 36);
  display.println(weatherDesc);
  display.setCursor(0, 50);
  display.print("Humidity: ");
  display.print(weatherHumidity);
  display.println("%");
  display.display();
}

void showDistance() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(20, 0);
  display.println("distance");
  float dist = calcDistance(myLat, myLon, LOC_B_LAT, LOC_B_LON);
  display.setTextSize(2);
  display.setCursor(0, 18);
  display.print(dist, 1);
  display.setTextSize(1);
  display.println(" mi");
  display.display();
}

void showClock() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  time_t now = time(nullptr);

  setenv("TZ", TIMEZONE_A, 1);
  tzset();
  struct tm tmA;
  localtime_r(&now, &tmA);

  setenv("TZ", TIMEZONE_B, 1);
  tzset();
  struct tm tmB;
  localtime_r(&now, &tmB);

  char strA[10], strB[10];
  char ampmA[3], ampmB[3];

  int hA = tmA.tm_hour;
  strcpy(ampmA, hA >= 12 ? "PM" : "AM");
  hA = hA % 12; if (hA == 0) hA = 12;
  sprintf(strA, "%d:%02d%s", hA, tmA.tm_min, ampmA);

  int hB = tmB.tm_hour;
  strcpy(ampmB, hB >= 12 ? "PM" : "AM");
  hB = hB % 12; if (hB == 0) hB = 12;
  sprintf(strB, "%d:%02d%s", hB, tmB.tm_min, ampmB);

  display.setTextSize(1);
  display.setCursor(5, 2);
  display.println(CLOCK_LABEL_A);
  display.setCursor(2, 18);
  display.println(strA);

  display.setCursor(70, 2);
  display.println(CLOCK_LABEL_B);
  display.setCursor(68, 18);
  display.println(strB);

  const char* dayNames[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  const char* monNames[] = {"Jan","Feb","Mar","Apr","May","Jun",
                             "Jul","Aug","Sep","Oct","Nov","Dec"};
  char dateStr[20];
  sprintf(dateStr, "%s %s %d", dayNames[tmA.tm_wday],
          monNames[tmA.tm_mon], tmA.tm_mday);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 50);
  display.println(dateStr);
  display.display();
}

void updateDisplay() {
  if      (currentScreen == 0) showClock();
  else if (currentScreen == 1) showWeather();
  else if (currentScreen == 2) {
    bool locationFresh = locationKnown &&
                        (millis() - locationReceivedAt < LOCATION_MAX_AGE);
    if (locationFresh) showDistance();
    else { currentScreen = (currentScreen + 1) % 4; updateDisplay(); }
  }
  else if (currentScreen == 3) showScreensaver();
}

void showNewMessage() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 5);
  display.println("New message!");
  display.setCursor(0, 20);
  display.println("Tap to reveal");
  display.display();
}

void showMessage(String msg) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextWrap(true);

  int lineCount = 1;
  for (int i = 0; i < (int)msg.length(); i++)
    if (msg[i] == '\n') lineCount++;
  int startY = max(0, (64 - (lineCount * 10)) / 2);

  String line = "";
  int y = startY;
  for (int i = 0; i <= (int)msg.length(); i++) {
    if (i == (int)msg.length() || msg[i] == '\n') {
      int16_t x1, y1; uint16_t w, h;
      display.getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
      int x = max(0, (128 - (int)w) / 2);
      display.setCursor(x, y);
      display.println(line);
      y += 10;
      line = "";
    } else {
      line += msg[i];
    }
  }
  display.display();
}

// =====================
// ANIMATIONS
// =====================
// Send these codes over MQTT to trigger animations instead of plain text.

void animateHeart() {
  int cx = 64, cy = 35;
  for (int pulse = 0; pulse < 3; pulse++) {
    display.clearDisplay();
    display.fillCircle(cx-8, cy-6, 10, SSD1306_WHITE);
    display.fillCircle(cx+8, cy-6, 10, SSD1306_WHITE);
    display.fillTriangle(cx-18, cy, cx+18, cy, cx, cy+18, SSD1306_WHITE);
    display.display();
    delay(400);
    display.clearDisplay();
    display.display();
    delay(200);
  }
  display.clearDisplay();
  display.display();
  delay(200);
}

void animateSunrise() {
  int cx = 64, cy = 28;
  for (int y = 64; y > cy; y -= 4) {
    display.clearDisplay();
    display.fillCircle(cx, y, 14, SSD1306_WHITE);
    for (int i = 0; i < 8; i++) {
      float angle = i * PI / 4;
      display.drawLine(cx+cos(angle)*18, y+sin(angle)*18,
                       cx+cos(angle)*24, y+sin(angle)*24, SSD1306_WHITE);
    }
    display.display();
    delay(60);
  }
  delay(500);
  display.clearDisplay();
  display.display();
  delay(200);
}

void animateMoon() {
  int cx = 80, cy = 28;
  for (int r = 0; r <= 16; r += 2) {
    display.clearDisplay();
    display.fillCircle(cx, cy, r, SSD1306_WHITE);
    display.fillCircle(cx-8, cy-4, r > 6 ? r-4 : 0, SSD1306_BLACK);
    display.drawPixel(20, 15, SSD1306_WHITE);
    display.drawPixel(35, 8,  SSD1306_WHITE);
    display.drawPixel(50, 20, SSD1306_WHITE);
    display.drawPixel(15, 30, SSD1306_WHITE);
    display.drawPixel(40, 35, SSD1306_WHITE);
    display.display();
    delay(80);
  }
  delay(500);
  display.clearDisplay();
  display.display();
  delay(200);
}

void animateFlower() {
  int cx = 64, cy = 28;
  int petalOffsets[8][2] = {{0,-14},{10,-10},{14,0},{10,10},{0,14},{-10,10},{-14,0},{-10,-10}};
  display.clearDisplay();
  display.display();
  for (int p = 0; p < 8; p++) {
    display.fillCircle(cx+petalOffsets[p][0], cy+petalOffsets[p][1], 6, SSD1306_WHITE);
    display.fillCircle(cx, cy, 7, SSD1306_WHITE);
    display.display();
    delay(120);
  }
  delay(500);
  display.clearDisplay();
  display.display();
  delay(200);
}

void animateFireworks() {
  for (int burst = 0; burst < 4; burst++) {
    int bx = 20 + random(88);
    int by = 10 + random(40);
    for (int r = 2; r <= 20; r += 3) {
      display.clearDisplay();
      for (int i = 0; i < 12; i++) {
        float angle = i * PI / 6;
        int x = bx + cos(angle) * r;
        int y = by + sin(angle) * r;
        display.drawPixel(x, y, SSD1306_WHITE);
        display.drawPixel(x+1, y, SSD1306_WHITE);
      }
      display.display();
      delay(40);
    }
    delay(100);
  }
  display.clearDisplay();
  display.display();
  delay(200);
}

void animateStars() {
  for (int frame = 0; frame < 20; frame++) {
    display.clearDisplay();
    for (int s = 0; s < 20; s++) {
      int x = random(128);
      int y = random(64);
      int size = random(3);
      if (size == 0) {
        display.drawPixel(x, y, SSD1306_WHITE);
      } else {
        display.drawLine(x-size, y, x+size, y, SSD1306_WHITE);
        display.drawLine(x, y-size, x, y+size, SSD1306_WHITE);
      }
    }
    display.display();
    delay(150);
  }
  display.clearDisplay();
  display.display();
  delay(200);
}

void playAnimation(String code) {
  if      (code == "__HEART__")     animateHeart();
  else if (code == "__SUNRISE__")   animateSunrise();
  else if (code == "__MOON__")      animateMoon();
  else if (code == "__FLOWER__")    animateFlower();
  else if (code == "__FIREWORKS__") animateFireworks();
  else if (code == "__STARS__")     animateStars();
}

bool isAnimationCode(String msg) {
  return msg == "__HEART__"     ||
         msg == "__SUNRISE__"   ||
         msg == "__MOON__"      ||
         msg == "__FLOWER__"    ||
         msg == "__FIREWORKS__" ||
         msg == "__STARS__";
}

// =====================
// SETUP
// =====================

void setup() {
  Serial.begin(115200);

  Wire.begin(18, 17);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found!");
    while (true);
  }
  display.clearDisplay();
  display.display();

  SPI.begin(41, 39, 40, 42);
  mfrc522.PCD_Init();

  connectWiFi();
  mqtt.subscribe(&mbox);
  mqtt.subscribe(&location);
  connectMQTT();

  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", TIMEZONE_A, 1);
  tzset();

  Serial.print("Syncing time");
  while (time(nullptr) < 100000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" done!");

  fetchWeather();
  showClock();
  Serial.println("Ready!");
}

// =====================
// LOOP
// =====================

void loop() {
  if (!mqtt.connected()) connectMQTT();

  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(100)) != NULL) {
    if (subscription == &mbox) {
      currentMessage = String((char *)mbox.lastread);
      newMessage = true;
      showNewMessage();
      Serial.println("New message: " + currentMessage);
    }
    if (subscription == &location) {
      String locStr = String((char *)location.lastread);
      int comma = locStr.indexOf(',');
      if (comma > 0) {
        myLat = locStr.substring(0, comma).toFloat();
        myLon = locStr.substring(comma + 1).toFloat();
        locationKnown = true;
        locationReceivedAt = millis();
      }
    }
  }

  // Screensaver animates continuously when active
  if (!newMessage && currentScreen == 3) {
    showScreensaver();
  }

  // Auto-rotate screens every 30 seconds
  if (!newMessage) {
    if (millis() - lastDisplaySwitch > DISPLAY_SWITCH_INTERVAL) {
      lastDisplaySwitch = millis();
      currentScreen = (currentScreen + 1) % 4;
      updateDisplay();
    }
  }

  // Fetch weather every 10 min
  if (millis() - lastWeatherFetch > WEATHER_FETCH_INTERVAL) {
    lastWeatherFetch = millis();
    fetchWeather();
  }

  // RFID tap to reveal message
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    byte *uid = mfrc522.uid.uidByte;
    byte size = mfrc522.uid.size;

    if (uidMatches(uid, cardUID, size) || uidMatches(uid, fobUID, size)) {
      if (newMessage) {
        newMessage = false;
        if (isAnimationCode(currentMessage)) {
          playAnimation(currentMessage);
          display.clearDisplay();
          display.display();
          delay(200);
          showMessage(currentMessage); // display the code as fallback text; customise as needed
        } else {
          showMessage(currentMessage);
        }
        delay(8000);
        updateDisplay();
      } else {
        currentScreen = (currentScreen + 1) % 4;
        updateDisplay();
        lastDisplaySwitch = millis();
        delay(500);
      }
    }
    mfrc522.PICC_HaltA();
  }
}
