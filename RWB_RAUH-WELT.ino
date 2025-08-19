#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "logo.h"
#include "backg.h"
#include "kiterRWB1.h"
#include "kiterRWB2.h"
#include "kiterRWB3.h"
#include "kiterRWB4.h"
#include "kiterRWB5.h"
#include "kiterRWB6.h"
#include "kiterRWB7.h"
#include "kiterRWB8.h"
#include <time.h>

// TFT display
TFT_eSPI tft = TFT_eSPI();

// Double buffering with two sprites
TFT_eSprite buffer1 = TFT_eSprite(&tft);
TFT_eSprite buffer2 = TFT_eSprite(&tft);
TFT_eSprite *activeBuffer = &buffer1;
TFT_eSprite *drawBuffer = &buffer2;

// Time/date/day sprite
TFT_eSprite timeSprite = TFT_eSprite(&tft);

// Kite sprites
TFT_eSprite kiterSprites[8] = {
  TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft),
  TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft)
};

// Kite bitmap info
const int kiteWidths[8] = {kiterRWB1_WIDTH, kiterRWB2_WIDTH, kiterRWB3_WIDTH, kiterRWB4_WIDTH,
                          kiterRWB5_WIDTH, kiterRWB6_WIDTH, kiterRWB7_WIDTH, kiterRWB8_WIDTH};
const int kiteHeights[8] = {kiterRWB1_HEIGHT, kiterRWB2_HEIGHT, kiterRWB3_HEIGHT, kiterRWB4_HEIGHT,
                           kiterRWB5_HEIGHT, kiterRWB6_HEIGHT, kiterRWB7_HEIGHT, kiterRWB8_HEIGHT};
const uint16_t* kiteBitmaps[8] = {kiterRWB1, kiterRWB2, kiterRWB3, kiterRWB4,
                                 kiterRWB5, kiterRWB6, kiterRWB7, kiterRWB8};

// Kite enabled flags
bool kiteEnabled[8] = {true, true, true, true, true, true, true, true};

// Time variables
struct tm timeinfo;
int timezoneIndex = 0;
const char* SDay[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

// Timezone configuration
struct Timezone {
  const char* name;
  const char* tzString;
};

Timezone timezones[] = {
  {"Zurich", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"UTC", "UTC0"},
  {"NewYork", "EST5EDT,M3.2.0/2,M11.1.0/2"},
  {"Tokyo", "JST-9"}
};
const int numTimezones = sizeof(timezones)/sizeof(timezones[0]);

// Animation variables
const int displayWidth = 320;
const int kiteCount = 8;
float kiteX[kiteCount] = { -300, -400, -900, -1200, -800, -1000, -1100, -1300 };
float kiteY[kiteCount] = { 5, 115, 12, 40, 40, 40, 40, 40 };

// Smooth float speeds in pixels/second
float kiteSpeed[kiteCount] = { 20, 20, 20, 20, 1.6, 10.2, 15.8, 12.4 };  
float kiteSpacing = 200;
unsigned long lastFrameTime = 0;

// Button
const int buttonPin = 0;
bool buttonPressed = false;


// ==========================
// Arduino SETUP
// ==========================
void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  pinMode(buttonPin, INPUT_PULLUP);

  // Initialize double buffers
  buffer1.createSprite(tft.width(), tft.height());
  buffer2.createSprite(tft.width(), tft.height());

  // Initialize time/date sprite
  timeSprite.createSprite(320, 170);
  timeSprite.setSwapBytes(true);

  // Initialize kite sprites
  for (int i = 0; i < kiteCount; i++) {
    kiterSprites[i].createSprite(kiteWidths[i], kiteHeights[i]);
    kiterSprites[i].setSwapBytes(false);
  }

  // Connect to WiFi and initialize time
  connectWiFi();
  initTime();
  setTimezone(timezoneIndex);

  lastFrameTime = millis();
}

// ==========================
// Arduino LOOP
// ==========================
void loop() {
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastFrameTime) / 1000.0f; // seconds
  lastFrameTime = currentTime;

  // Handle button press
  checkTimeZoneButton();
  
  // Update time
  updateTime();
  
  // Update kite positions
  updateKitePositions(deltaTime);
  
  // Draw everything to off-screen buffer
  drawFrame();
  
  // Swap and display buffers
  swapBuffers();
}


// ==========================
// FUNCTIONS
// ==========================

void connectWiFi() {
  WiFiManager wm;
  bool res = wm.autoConnect("ESP32_Clock");
  if(!res) {
    Serial.println("Failed to connect");
  } else {
    Serial.println("Connected to WiFi");
  }
}

void initTime() {
  configTime(0, 0, "pool.ntp.org");
  delay(1000);
}

void setTimezone(int index) {
  setenv("TZ", timezones[index].tzString, 1);
  tzset();
}

void updateTime() {
  if(!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  }
}

void checkTimeZoneButton() {
  if (digitalRead(buttonPin) == LOW && !buttonPressed) {
    buttonPressed = true;
    timezoneIndex = (timezoneIndex + 1) % numTimezones;
    setTimezone(timezoneIndex);
    updateTime();
    delay(200);
  } else if (digitalRead(buttonPin) == HIGH) {
    buttonPressed = false;
  }
}

void updateKitePositions(float deltaTime) {
  for (int i = 0; i < kiteCount; i++) {
    if (!kiteEnabled[i]) continue;

    // Move smoothly using delta time
    kiteX[i] += kiteSpeed[i] * deltaTime;

    // Wrap around screen
    if (kiteX[i] > displayWidth + kiteWidths[i]) {
      float minX = kiteX[0];
      for (int j = 1; j < kiteCount; j++) {
        if (kiteX[j] < minX) minX = kiteX[j];
      }
      kiteX[i] = minX - kiteWidths[i] - kiteSpacing;
    }
  }
}

void drawFrame() {
  drawBuffer->fillSprite(TFT_BLACK);
  
  // Draw background
  drawBuffer->pushImage(0, 0, 320, 170, backg);
  
  // Draw kites
  for (int i = 0; i < kiteCount; i++) {
    if (kiteEnabled[i]) {
      kiterSprites[i].pushImage(0, 0, kiteWidths[i], kiteHeights[i], kiteBitmaps[i]);
      // Cast directly to int (fractional motion preserved internally)
      kiterSprites[i].pushToSprite(drawBuffer, (int)kiteX[i], (int)kiteY[i], TFT_BLACK);
    }
  }
  
  // Draw time/date/day
  drawTime();
}

void drawTime() {
  timeSprite.fillSprite(TFT_BLACK);

  // Large time
  timeSprite.setTextColor(TFT_GOLD, TFT_BLACK);
  timeSprite.setTextFont(7);
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  timeSprite.drawString(timeStr, 0, 0);
  timeSprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
  // Date
  timeSprite.setTextFont(4);
  timeSprite.drawString(
    String(timeinfo.tm_mon + 1) + "/" +
    String(timeinfo.tm_mday) + "/" +
    String(timeinfo.tm_year + 1900),145, 50
    
  );
timeSprite.setTextFont(2);
timeSprite.setTextColor(TFT_GOLD, TFT_BLACK);
  // Day
  timeSprite.drawString(SDay[timeinfo.tm_wday], 235, 15);

  // Push sprite to main buffer
  timeSprite.pushToSprite(drawBuffer, 50, 0, TFT_BLACK);

  // Logo
  drawBuffer->pushImage(2, 0, 20, 20, logo);
}

void swapBuffers() {
  drawBuffer->pushSprite(0, 0);
  TFT_eSprite* temp = activeBuffer;
  activeBuffer = drawBuffer;
  drawBuffer = temp;
}
