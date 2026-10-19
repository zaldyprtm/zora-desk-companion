#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FluxGarage_RoboEyes.h>
#include <ChronosESP32.h>

// ============================================================
// ZORA - ESP32-C3 SUPER MINI
// RoboEyes + Chronos BLE + Clock + Weather + Navigation
// ============================================================

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDR 0x3C
#define OLED_SDA 20
#define OLED_SCL 21
#define TOUCH_PIN 5
#define BUZZER_PIN 6
#define TOUCH_ACTIVE_LEVEL HIGH

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
RoboEyes<Adafruit_SSD1306> roboEyes(display);
Preferences preferences;
ChronosESP32 chronosWatch("ZORA");

enum AppMode {
  MODE_HOME,
  MODE_SETTINGS,
  MODE_VOLUME,
  MODE_NOTIFICATION,
  MODE_CLOCK,
  MODE_WEATHER,
  MODE_NAVIGATION
};

AppMode currentMode = MODE_HOME;

int volumeLevel = 65;
const int VOLUME_MIN = 0;
const int VOLUME_MAX = 100;
const int VOLUME_STEP = 10;

bool bleConnected = false;
bool notificationAvailable = false;

String lastNotificationTitle = "";
String lastNotificationBody = "";

const char* settingItems[] = {
  "Volume",
  "Chronos BLE",
  "Notifications",
  "Back Home"
};

const int SETTINGS_COUNT =
  sizeof(settingItems) / sizeof(settingItems[0]);

int settingsIndex = 0;

// ============================================================
// TOUCH
// ============================================================

bool touchRawPrevious = false;
bool touchStable = false;
bool touchLongHandled = false;

unsigned long touchChangedAt = 0;
unsigned long touchPressedAt = 0;
unsigned long lastTapAt = 0;
unsigned long lastTouchActionAt = 0;

const unsigned long TOUCH_DEBOUNCE_MS = 45;
const unsigned long TAP_GAP_MS = 380;
const unsigned long LONG_PRESS_MS = 900;
const unsigned long TOUCH_LOCKOUT_MS = 100;

int tapCount = 0;

// ============================================================
// BUZZER / MELODIES
// ============================================================

struct MelodyNote {
  uint16_t frequency;
  uint16_t duration;
};

MelodyNote melodyNotes[8];
uint8_t melodyLength = 0;
uint8_t melodyIndex = 0;
uint8_t touchMelodyIndex = 0;

bool melodyPlaying = false;
bool buzzerActive = false;

unsigned long buzzerEndsAt = 0;
unsigned long nextMelodyNoteAt = 0;

void stopMelody() {
  melodyPlaying = false;
  buzzerActive = false;
  melodyLength = 0;
  melodyIndex = 0;
  noTone(BUZZER_PIN);
}

void startMelody(const MelodyNote* notes, uint8_t count) {
  stopMelody();

  if (volumeLevel <= 0 || count == 0) return;
  if (count > 8) count = 8;

  for (uint8_t i = 0; i < count; i++) {
    melodyNotes[i] = notes[i];
  }

  melodyLength = count;
  melodyIndex = 0;
  melodyPlaying = true;
  nextMelodyNoteAt = millis();
}

void updateBuzzer() {
  unsigned long now = millis();

  if (buzzerActive && (long)(now - buzzerEndsAt) >= 0) {
    noTone(BUZZER_PIN);
    buzzerActive = false;
  }

  if (!melodyPlaying ||
      (long)(now - nextMelodyNoteAt) < 0) {
    return;
  }

  if (melodyIndex >= melodyLength) {
    stopMelody();
    return;
  }

  MelodyNote note = melodyNotes[melodyIndex++];

  unsigned long duration =
    max(1, (int)note.duration * volumeLevel / 100);

  if (note.frequency > 0 && volumeLevel > 0) {
    tone(BUZZER_PIN, note.frequency);
    buzzerActive = true;
  } else {
    noTone(BUZZER_PIN);
    buzzerActive = false;
  }

  buzzerEndsAt = now + duration;
  nextMelodyNoteAt = now + duration + 18;
}

void playTapSound() {
  const MelodyNote melodies[8][4] = {
    {{523,75},{659,75},{784,100},{0,0}},
    {{659,70},{784,70},{988,110},{0,0}},
    {{784,65},{988,65},{1175,100},{0,0}},
    {{784,70},{659,70},{523,110},{0,0}},
    {{523,65},{659,65},{784,65},{1047,130}},
    {{988,65},{784,65},{659,100},{0,0}},
    {{587,70},{740,70},{880,70},{1175,120}},
    {{1047,65},{880,65},{784,65},{659,120}}
  };

  const uint8_t counts[8] = {3,3,3,3,4,3,4,4};

  startMelody(
    melodies[touchMelodyIndex],
    counts[touchMelodyIndex]
  );

  touchMelodyIndex = (touchMelodyIndex + 1) % 8;
}

void playSelectSound() {
  const MelodyNote n[] = {
    {784,65},{988,65},{1175,65},{1568,120}
  };
  startMelody(n, 4);
}

void playBackSound() {
  const MelodyNote n[] = {
    {1047,80},{880,80},{698,130}
  };
  startMelody(n, 3);
}

void playConfirmSound() {
  const MelodyNote n[] = {
    {659,60},{784,60},{988,60},{1319,150}
  };
  startMelody(n, 4);
}

void playErrorSound() {
  const MelodyNote n[] = {
    {440,100},{349,150},{262,200}
  };
  startMelody(n, 3);
}

// ============================================================
// SCREEN CACHE / SETTINGS
// ============================================================

AppMode lastDrawnMode = (AppMode)255;
int lastDrawnSettingsIndex = -1;
int lastDrawnVolume = -1;
bool lastDrawnBle = false;

void resetScreenCache() {
  lastDrawnMode = (AppMode)255;
  lastDrawnSettingsIndex = -1;
  lastDrawnVolume = -1;
  lastDrawnBle = !bleConnected;
}

void saveSettings() {
  preferences.begin("zora", false);
  preferences.putInt("volume", volumeLevel);
  preferences.end();
}

void loadSettings() {
  preferences.begin("zora", true);
  volumeLevel = preferences.getInt("volume", 65);
  preferences.end();

  volumeLevel = constrain(
    volumeLevel, VOLUME_MIN, VOLUME_MAX
  );
}

// ============================================================
// BOOT / HOME
// ============================================================

void showBootSplash() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(28, 8);
  display.println("ZORA");

  display.setTextSize(1);
  display.setCursor(31, 32);
  display.println("BY ZDYVOID");

  display.setCursor(16, 46);
  display.println("YOUR COMPANION");

  display.display();
  delay(1200);

  display.clearDisplay();
  display.display();
}

void returnHome() {
  currentMode = MODE_HOME;
  tapCount = 0;

  roboEyes.setMood(DEFAULT);
  roboEyes.setPosition(DEFAULT);
  roboEyes.setAutoblinker(ON, 3, 2);
  roboEyes.setIdleMode(ON, 2, 2);

  resetScreenCache();
  playBackSound();
}

// ============================================================
// CLOCK
// ============================================================

void drawClockScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("ZORA CLOCK");

  display.setCursor(94, 0);
  display.print(bleConnected ? "BLE+" : "BLE-");

  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  if (!bleConnected) {
    display.setCursor(7, 25);
    display.println("Connect Chronos app");

    display.setCursor(7, 38);
    display.println("to sync time.");
  } else {
    String timeText = chronosWatch.getTime("%H:%M:%S");

    display.setTextSize(2);
    display.setCursor(15, 19);
    display.println(timeText);

    display.setTextSize(1);
    display.setCursor(8, 46);
    display.println(
      chronosWatch.getTime("%a, %d/%m/%Y")
    );
  }

  display.setCursor(0, 57);
  display.print("Tap:Home");
  display.display();
}

// ============================================================
// WEATHER
// ============================================================

void drawWeatherScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("ZORA WEATHER");

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  int count = chronosWatch.getWeatherCount();

  if (!bleConnected) {
    display.setCursor(0, 22);
    display.println("Chronos disconnected");

    display.setCursor(0, 35);
    display.println("Connect phone first.");
  } else if (count <= 0) {
    display.setCursor(0, 22);
    display.println("No weather data.");

    display.setCursor(0, 35);
    display.println("Check Chronos app.");
  } else {
    Weather &w = chronosWatch.getWeatherAt(0);

    String city = chronosWatch.getWeatherCity();

    if (city.length() > 20) {
      city = city.substring(0, 20);
    }

    display.setCursor(0, 13);
    display.println(city.length() ? city : "Current weather");

    display.setTextSize(2);
    display.setCursor(0, 25);
    display.print(w.temp);
    display.print(" C");

    display.setTextSize(1);
    display.setCursor(68, 27);
    display.print("Icon ");
    display.println(w.icon);

    display.setCursor(0, 47);
    display.print("High ");
    display.print(w.high);
    display.print("C Low ");
    display.print(w.low);
    display.println("C");
  }

  display.setCursor(0, 57);
  display.print("Tap:Home");

  display.display();
}

// ============================================================
// NAVIGATION
// ============================================================

void drawNavigationArrow(String directions) {
  directions.toLowerCase();

  if (directions.indexOf("left") >= 0 ||
      directions.indexOf("kiri") >= 0) {

    display.drawLine(40,34,75,34,SSD1306_WHITE);
    display.drawLine(40,34,52,22,SSD1306_WHITE);
    display.drawLine(40,34,52,46,SSD1306_WHITE);

  } else if (directions.indexOf("right") >= 0 ||
             directions.indexOf("kanan") >= 0) {

    display.drawLine(40,34,75,34,SSD1306_WHITE);
    display.drawLine(75,34,63,22,SSD1306_WHITE);
    display.drawLine(75,34,63,46,SSD1306_WHITE);

  } else {
    display.drawLine(57,47,57,20,SSD1306_WHITE);
    display.drawLine(57,20,45,32,SSD1306_WHITE);
    display.drawLine(57,20,69,32,SSD1306_WHITE);
  }
}

void drawNavigationScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("ZORA NAVIGATION");

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  Navigation &nav = chronosWatch.getNavigation();

  if (!bleConnected) {
    display.setCursor(0, 23);
    display.println("Chronos disconnected");

    display.setCursor(0, 36);
    display.println("Connect phone first.");

  } else if (!nav.active) {
    display.setCursor(0, 23);
    display.println("No active route.");

    display.setCursor(0, 36);
    display.println("Start Google Maps.");

  } else {
    String instruction =
      nav.directions.length() ? nav.directions : nav.title;

    if (instruction.length() > 21) {
      instruction = instruction.substring(0, 21);
    }

    display.setCursor(0, 13);
    display.println(instruction);

    drawNavigationArrow(nav.directions);

    display.setCursor(0, 49);
    display.print("Dist: ");
    display.print(nav.distance);

    display.setCursor(0, 58);
    display.print("ETA: ");
    display.print(nav.eta);
  }

  display.display();
}

// ============================================================
// NOTIFICATIONS
// ============================================================

void drawNotificationScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("NOTIFICATION");

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  if (!notificationAvailable ||
      lastNotificationTitle.length() == 0) {

    display.setCursor(0, 19);
    display.println("No notification yet");

    display.setCursor(0, 33);
    display.println("Check Chronos access");

    display.setCursor(0, 45);
    display.println("to notifications.");

  } else {
    String title = lastNotificationTitle;
    String body = lastNotificationBody;

    if (title.length() > 21) {
      title = title.substring(0, 21);
    }

    display.setCursor(0, 14);
    display.println(title);

    if (body.length() > 21) {
      body = body.substring(0, 21);
    }

    display.setCursor(0, 29);
    display.println(body);

    if (lastNotificationBody.length() > 21) {
      String line2 = lastNotificationBody.substring(
        21, min(42, (int)lastNotificationBody.length())
      );

      display.setCursor(0, 40);
      display.println(line2);
    }

    if (lastNotificationBody.length() > 42) {
      String line3 = lastNotificationBody.substring(
        42, min(63, (int)lastNotificationBody.length())
      );

      display.setCursor(0, 50);
      display.println(line3);
    }
  }

  display.display();
}

// ============================================================
// SETTINGS / VOLUME
// ============================================================

void drawSettings() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("ZORA SETTINGS");

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  for (int i = 0; i < SETTINGS_COUNT; i++) {
    int y = 13 + i * 10;

    if (i == settingsIndex) {
      display.fillRect(0, y, 128, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(2, y);
    display.print(i == settingsIndex ? ">" : " ");
    display.print(settingItems[i]);

    if (i == 1) {
      display.setCursor(99, y);
      display.print(chronosWatch.isRunning() ? "ON" : "OFF");
    }
  }

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 57);
  display.print("1:Next 2:Select");
  display.display();
}

void drawVolumeScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("VOLUME SETTINGS");

  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(37, 20);
  display.print(volumeLevel);
  display.println("%");

  display.drawRect(8, 45, 112, 9, SSD1306_WHITE);

  int width = map(volumeLevel, 0, 100, 0, 106);

  if (width > 0) {
    display.fillRect(11, 48, width, 3, SSD1306_WHITE);
  }

  display.setTextSize(1);
  display.setCursor(0, 57);
  display.print("1:+10 2:-10 3:Back");

  display.display();
}

void changeVolume(int delta) {
  int oldVolume = volumeLevel;

  volumeLevel = constrain(
    volumeLevel + delta, VOLUME_MIN, VOLUME_MAX
  );

  if (oldVolume != volumeLevel) {
    saveSettings();
  }

  playTapSound();
  resetScreenCache();
}

void selectSetting() {
  switch (settingsIndex) {
    case 0:
      currentMode = MODE_VOLUME;
      resetScreenCache();
      playConfirmSound();
      break;

    case 1:
      if (chronosWatch.isRunning()) {
        chronosWatch.stop(true);
        bleConnected = false;
        Serial.println("[BLE] Stop requested");
      } else {
        chronosWatch.begin();
        Serial.println("[BLE] Start requested");
      }

      resetScreenCache();
      break;

    case 2:
      currentMode = MODE_NOTIFICATION;
      resetScreenCache();
      playSelectSound();
      break;

    case 3:
      returnHome();
      break;
  }
}

// ============================================================
// CHRONOS BLE CALLBACKS
// ============================================================

void onChronosConnection(bool connected) {
  bleConnected = connected;

  Serial.println(
    connected ? "[BLE] Connected" : "[BLE] Disconnected"
  );

  resetScreenCache();
}

void onChronosNotification(Notification notification) {
  lastNotificationTitle = notification.title;
  lastNotificationBody = notification.message;
  notificationAvailable = true;

  Serial.println();
  Serial.println("====== NOTIFICATION RECEIVED ======");
  Serial.print("Title: ");
  Serial.println(lastNotificationTitle);
  Serial.print("Message: ");
  Serial.println(lastNotificationBody);
  Serial.println("===================================");

  // Auto-show when Home is active.
  if (currentMode == MODE_HOME) {
    currentMode = MODE_NOTIFICATION;
    resetScreenCache();
  }
}

void onChronosConfiguration(
  Config config,
  uint32_t arg1,
  uint32_t arg2
) {
  Serial.printf(
    "[CHRONOS CONFIG] config=%d arg1=%lu arg2=%lu\n",
    (int)config,
    (unsigned long)arg1,
    (unsigned long)arg2
  );

  if (config == CF_TIME ||
      config == CF_WEATHER ||
      config == CF_NAV_DATA ||
      config == CF_NAV_ICON) {
    resetScreenCache();
  }

  if (config == CF_NAV_DATA) {
    Navigation &nav = chronosWatch.getNavigation();

    Serial.println("[NAV] Navigation data event");
    Serial.printf("Active: %d\n", nav.active);

    Serial.print("Title: ");
    Serial.println(nav.title);

    Serial.print("Directions: ");
    Serial.println(nav.directions);

    Serial.print("Distance: ");
    Serial.println(nav.distance);

    Serial.print("ETA: ");
    Serial.println(nav.eta);
  }
}

// ============================================================
// TOUCH ACTIONS
// ============================================================

void handleTapCount(int count) {
  switch (currentMode) {
    case MODE_HOME:
      if (count == 3) {
        currentMode = MODE_CLOCK;
        resetScreenCache();
        playSelectSound();
      } else if (count == 4) {
        currentMode = MODE_WEATHER;
        resetScreenCache();
        playSelectSound();
      } else if (count >= 5) {
        currentMode = MODE_SETTINGS;
        settingsIndex = 0;
        resetScreenCache();
        playConfirmSound();
      }
      break;

    case MODE_SETTINGS:
      if (count == 1) {
        settingsIndex = (settingsIndex + 1) % SETTINGS_COUNT;
        resetScreenCache();
        playTapSound();
      } else if (count == 2) {
        selectSetting();
      } else {
        returnHome();
      }
      break;

    case MODE_VOLUME:
      if (count == 1) {
        changeVolume(VOLUME_STEP);
      } else if (count == 2) {
        changeVolume(-VOLUME_STEP);
      } else {
        currentMode = MODE_SETTINGS;
        resetScreenCache();
        playBackSound();
      }
      break;

    case MODE_NOTIFICATION:
    case MODE_CLOCK:
    case MODE_WEATHER:
    case MODE_NAVIGATION:
      returnHome();
      break;
  }
}

void handleLongPress() {
  switch (currentMode) {
    case MODE_HOME:
      currentMode = MODE_SETTINGS;
      settingsIndex = 0;
      resetScreenCache();
      playConfirmSound();
      break;

    case MODE_SETTINGS:
    case MODE_VOLUME:
    case MODE_NOTIFICATION:
    case MODE_CLOCK:
    case MODE_WEATHER:
    case MODE_NAVIGATION:
      returnHome();
      break;
  }
}

void updateTouch() {
  unsigned long now = millis();

  bool raw =
    digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL;

  if (raw != touchRawPrevious) {
    touchRawPrevious = raw;
    touchChangedAt = now;
  }

  if (now - touchChangedAt >= TOUCH_DEBOUNCE_MS &&
      raw != touchStable) {

    touchStable = raw;

    if (touchStable) {
      touchPressedAt = now;
      touchLongHandled = false;

      if (currentMode == MODE_HOME) {
        playTapSound();
        roboEyes.setMood(HAPPY);
        roboEyes.setPosition(random(0, 2) == 0 ? E : W);
      }

    } else if (!touchLongHandled &&
               now - lastTouchActionAt >= TOUCH_LOCKOUT_MS) {

      lastTouchActionAt = now;
      tapCount++;
      lastTapAt = now;
    }
  }

  if (touchStable &&
      !touchLongHandled &&
      now - touchPressedAt >= LONG_PRESS_MS) {

    touchLongHandled = true;
    tapCount = 0;
    handleLongPress();
  }

  if (tapCount > 0 &&
      now - lastTapAt > TAP_GAP_MS) {

    int count = tapCount;
    tapCount = 0;
    handleTapCount(count);
  }
}

// ============================================================
// AUTOMATIC NAVIGATION OVERRIDE
// ============================================================

void updateNavigationOverride() {
  if (!bleConnected) {
    if (currentMode == MODE_NAVIGATION) {
      currentMode = MODE_HOME;
      resetScreenCache();
    }
    return;
  }

  Navigation &nav = chronosWatch.getNavigation();

  if (currentMode == MODE_HOME && nav.active) {
    Serial.println("[NAV] Taking over Home screen");
    currentMode = MODE_NAVIGATION;
    resetScreenCache();

  } else if (currentMode == MODE_NAVIGATION &&
             !nav.active) {

    Serial.println("[NAV] Route inactive; returning Home");
    currentMode = MODE_HOME;
    resetScreenCache();
  }
}

// ============================================================
// SCREEN RENDERING
// ============================================================

void renderCurrentScreen() {
  switch (currentMode) {
    case MODE_HOME:
      roboEyes.update();
      break;

    case MODE_SETTINGS:
      if (lastDrawnMode != MODE_SETTINGS ||
          lastDrawnSettingsIndex != settingsIndex ||
          lastDrawnBle != chronosWatch.isRunning()) {
        drawSettings();
        lastDrawnSettingsIndex = settingsIndex;
        lastDrawnBle = chronosWatch.isRunning();
      }
      break;

    case MODE_VOLUME:
      if (lastDrawnMode != MODE_VOLUME ||
          lastDrawnVolume != volumeLevel) {
        drawVolumeScreen();
        lastDrawnVolume = volumeLevel;
      }
      break;

    case MODE_NOTIFICATION:
      if (lastDrawnMode != MODE_NOTIFICATION) {
        drawNotificationScreen();
      }
      break;

    case MODE_CLOCK:
      drawClockScreen();
      break;

    case MODE_WEATHER:
      drawWeatherScreen();
      break;

    case MODE_NAVIGATION:
      drawNavigationScreen();
      break;
  }

  lastDrawnMode = currentMode;
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(150);

  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[OLED] Initialization failed!");
    while (true) delay(100);
  }

  display.clearDisplay();
  display.display();

  loadSettings();
  showBootSplash();

  roboEyes.begin(OLED_WIDTH, OLED_HEIGHT, 100);
  roboEyes.setAutoblinker(ON, 3, 2);
  roboEyes.setIdleMode(ON, 2, 2);
  roboEyes.setMood(DEFAULT);
  roboEyes.setPosition(DEFAULT);

  chronosWatch.setConnectionCallback(onChronosConnection);
  chronosWatch.setNotificationCallback(onChronosNotification);
  chronosWatch.setConfigurationCallback(onChronosConfiguration);

  chronosWatch.begin();

  randomSeed(micros());
  resetScreenCache();

  Serial.println("[ZORA] Ready");
  Serial.println("[ZORA] Serial Monitor: 115200 baud");
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
  updateTouch();

  // Chronos BLE needs continuous polling.
  chronosWatch.loop();

  updateBuzzer();
  updateNavigationOverride();
  renderCurrentScreen();

  delay(1);
}