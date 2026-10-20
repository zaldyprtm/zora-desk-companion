
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ChronosESP32.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <FluxGarage_RoboEyes.h>

#undef N
#undef E

// RoboEyes direction values (avoid N/E macro collisions with ESP32 mbedTLS)
static constexpr int ZORA_EYE_POSITION_EAST = 3;
static constexpr int ZORA_EYE_POSITION_WEST = 7;

// ============================================================
// ZORA - ESP32-C3 SUPER MINI
// RoboEyes + Manual BLE + Clock + Weather + Navigation
// Notification + Dino Jump + Manual Wi-Fi GitHub Releases OTA
// ============================================================

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDR 0x3C
#define OLED_SDA 20
#define OLED_SCL 21
#define TOUCH_PIN 5
#define BUZZER_PIN 6
#define TOUCH_ACTIVE_LEVEL HIGH

// ============================================================
// OTA CONFIGURATION
// IMPORTANT: fill in your Wi-Fi password before compiling.
// Create a PUBLIC GitHub Release and attach a compiled .bin file.
// This prototype currently uses setInsecure() for TLS; see notes
// after the code before using it for a real firmware update.
// ============================================================
#define ZORA_FIRMWARE_VERSION "1.0.0"
const char* WIFI_SSID = "ZACKDIR";
const char* WIFI_PASSWORD = "rosenbloom31";
const char* GITHUB_RELEASE_API = "https://api.github.com/repos/zaldyprtm/zora-desk-companion/releases/latest";
const char* GITHUB_USER_AGENT = "ZORA-Desk-Companion";
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000UL;

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
  MODE_NAVIGATION,
  MODE_DINO,
  MODE_OTA
};

AppMode currentMode = MODE_HOME;

int volumeLevel = 65;
const int VOLUME_MIN = 0;
const int VOLUME_MAX = 100;
const int VOLUME_STEP = 10;

bool bleEnabled = false;
bool bleConnected = false;
bool notificationAvailable = false;

String lastNotificationTitle = "";
String lastNotificationBody = "";

const char* settingItems[] = {
  "Volume",
  "Chronos BLE",
  "Notifications",
  "Dino Jump",
  "WiFi for OTA",
  "Firmware Update",
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

  startMelody(melodies[touchMelodyIndex],
              counts[touchMelodyIndex]);

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

void playWinSound() {
  const MelodyNote n[] = {
    {784,70},{988,70},{1319,180}
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

// OTA UI/state
String otaLatestVersion = "Unknown";
String otaFirmwareUrl = "";
String otaStatusMessage = "Tap 1x to check";
bool otaReleaseFound = false;
bool otaBusy = false;
bool otaWiFiWasConnected = false;
bool wifiManualConnecting = false;

// Happy expression timeout (non-blocking)
bool happyActive = false;
unsigned long happyStartedAt = 0;
const unsigned long HAPPY_DURATION_MS = 5000UL;

void resetScreenCache() {
  lastDrawnMode = (AppMode)255;
  lastDrawnSettingsIndex = -1;
  lastDrawnVolume = -1;
  lastDrawnBle = !bleEnabled;
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
  display.println("ZORA BOT");

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
    display.println("Enable Chronos BLE");

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

  int count = bleConnected
    ? chronosWatch.getWeatherCount()
    : 0;

  if (!bleConnected) {
    display.setCursor(0, 22);
    display.println("Chronos disconnected");

    display.setCursor(0, 35);
    display.println("Enable BLE in Settings.");

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
    display.println("Enable BLE first.");

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
    display.println("on your phone.");

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

    display.setCursor(0, 27);
    display.println(body);

    if (lastNotificationBody.length() > 21) {
      String line2 = lastNotificationBody.substring(
        21, min(42, (int)lastNotificationBody.length())
      );
      display.setCursor(0, 38);
      display.println(line2);
    }

    if (lastNotificationBody.length() > 42) {
      String line3 = lastNotificationBody.substring(
        42, min(63, (int)lastNotificationBody.length())
      );
      display.setCursor(0, 49);
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

  // Show up to six rows at a time so the menu remains usable on 128x64.
  int first = settingsIndex - 5;
  if (first < 0) first = 0;
  for (int row = 0; row < 6; row++) {
    int i = first + row;
    if (i >= SETTINGS_COUNT) break;
    int y = 12 + row * 8;

    if (i == settingsIndex) {
      display.fillRect(0, y, 128, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(2, y);
    display.print(i == settingsIndex ? ">" : " ");
    display.print(settingItems[i]);
    if (i == 1) {
      display.setCursor(99, y);
      display.print(bleEnabled ? "ON" : "OFF");
    } else if (i == 4) {
      display.setCursor(92, y);
      display.print(WiFi.status() == WL_CONNECTED ? "ON" : "OFF");
    }
  }
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 61);
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

  if (oldVolume != volumeLevel) saveSettings();

  playTapSound();
  resetScreenCache();
}

// ============================================================
// MINI GAME: ZORA DINO JUMP
// Touch once to jump over the cactus. Tap again after game over.
// ============================================================
bool dinoRunning = false;
bool dinoGameOver = false;
bool dinoJumping = false;
unsigned long dinoJumpStartedAt = 0;
unsigned long dinoLastFrameAt = 0;
int dinoObstacleX = 128;
int dinoScore = 0;
int dinoSpeed = 3;
const unsigned long DINO_JUMP_MS = 620;
const int DINO_GROUND_Y = 51;

void startDinoGame() {
  dinoRunning = true;
  dinoGameOver = false;
  dinoJumping = false;
  dinoObstacleX = 128;
  dinoScore = 0;
  dinoSpeed = 3;
  dinoLastFrameAt = millis();
  currentMode = MODE_DINO;
  resetScreenCache();
  playConfirmSound();
}

void dinoGameAction() {
  if (dinoGameOver || !dinoRunning) {
    startDinoGame();
    return;
  }
  if (!dinoJumping) {
    dinoJumping = true;
    dinoJumpStartedAt = millis();
    playTapSound();
  }
}

void updateDinoGame() {
  if (!dinoRunning || dinoGameOver) return;
  unsigned long now = millis();
  if (dinoJumping && now - dinoJumpStartedAt >= DINO_JUMP_MS) {
    dinoJumping = false;
  }
  if (now - dinoLastFrameAt >= 35) {
    dinoLastFrameAt = now;
    dinoObstacleX -= dinoSpeed;
    if (dinoObstacleX < -8) {
      dinoObstacleX = 128 + random(0, 35);
      dinoScore++;
      if (dinoScore % 5 == 0 && dinoSpeed < 6) dinoSpeed++;
    }
  }
  int dinoY = DINO_GROUND_Y;
  if (dinoJumping) {
    float phase = (float)(now - dinoJumpStartedAt) / DINO_JUMP_MS;
    if (phase > 1.0f) phase = 1.0f;
    dinoY = DINO_GROUND_Y - (int)(25.0f * 4.0f * phase * (1.0f - phase));
  }
  // Collision is checked against the dino's feet/body and cactus.
  if (dinoObstacleX > 17 && dinoObstacleX < 30 &&
      dinoY > DINO_GROUND_Y - 13) {
    dinoGameOver = true;
    dinoRunning = false;
    playErrorSound();
  }
}

void drawDinoGame() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ZORA DINO  SCORE:");
  display.print(dinoScore);
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);
  display.drawLine(0, DINO_GROUND_Y + 7, 127, DINO_GROUND_Y + 7, SSD1306_WHITE);

  int dinoY = DINO_GROUND_Y;
  if (dinoJumping) {
    long elapsed = (long)(millis() - dinoJumpStartedAt);
    float phase = (float)elapsed / DINO_JUMP_MS;
    if (phase > 1.0f) phase = 1.0f;
    dinoY = DINO_GROUND_Y - (int)(25.0f * 4.0f * phase * (1.0f - phase));
  }
  // Pixel dino: head, body, eye, and legs.
  display.fillRect(10, dinoY - 12, 10, 9, SSD1306_WHITE);
  display.fillRect(16, dinoY - 17, 7, 7, SSD1306_WHITE);
  display.drawPixel(21, dinoY - 14, SSD1306_BLACK);
  display.fillRect(10, dinoY - 3, 4, 7, SSD1306_WHITE);
  display.fillRect(17, dinoY - 3, 4, 7, SSD1306_WHITE);
  // Cactus obstacle
  display.fillRect(dinoObstacleX, DINO_GROUND_Y - 9, 5, 16, SSD1306_WHITE);
  display.fillRect(dinoObstacleX - 3, DINO_GROUND_Y - 5, 3, 5, SSD1306_WHITE);
  display.fillRect(dinoObstacleX + 5, DINO_GROUND_Y - 7, 3, 5, SSD1306_WHITE);

  if (dinoGameOver) {
    display.fillRect(27, 18, 75, 25, SSD1306_BLACK);
    display.drawRect(27, 18, 75, 25, SSD1306_WHITE);
    display.setCursor(38, 22);
    display.println("GAME OVER");
    display.setCursor(32, 33);
    display.print("Score ");
    display.print(dinoScore);
    display.setCursor(0, 57);
    display.print("Tap: retry  Hold: exit");
  } else {
    display.setCursor(0, 57);
    display.print("Tap: jump  Hold: exit");
  }
  display.display();
}

// ============================================================
// BLE CONTROL
// ============================================================

void toggleChronosBLE() {
  if (bleEnabled) {
    chronosWatch.stop(true);
    bleEnabled = false;
    bleConnected = false;

    Serial.println("[BLE] Disabled from Settings");
    playBackSound();

  } else {
    bleEnabled = true;

    Serial.println("[BLE] Starting Chronos...");
    chronosWatch.begin();

    playConfirmSound();
  }

  resetScreenCache();
}

// ============================================================
// GITHUB RELEASE OTA
// ============================================================

String extractJsonString(const String& json, const String& key, int fromIndex = 0) {
  String needle = "\"" + key + "\"";
  int keyPos = json.indexOf(needle, fromIndex);
  if (keyPos < 0) return "";
  int colon = json.indexOf(':', keyPos + needle.length());
  if (colon < 0) return "";
  int firstQuote = json.indexOf('"', colon + 1);
  if (firstQuote < 0) return "";
  int endQuote = json.indexOf('"', firstQuote + 1);
  if (endQuote < 0) return "";
  return json.substring(firstQuote + 1, endQuote);
}

bool connectWiFiForOTA() {
  // Wi-Fi must be enabled manually from Settings first.
  if (WiFi.status() == WL_CONNECTED) return true;
  otaStatusMessage = "Enable WiFi in Settings";
  return false;
}

void cleanupOTAWiFi() {
  // Keep Wi-Fi state under manual user control.
  otaWiFiWasConnected = (WiFi.status() == WL_CONNECTED);
}

bool checkLatestGitHubRelease() {
  otaBusy = true;
  otaStatusMessage = "Connecting WiFi...";
  resetScreenCache();
  if (!connectWiFiForOTA()) {
    cleanupOTAWiFi();
    otaBusy = false;
    resetScreenCache();
    return false;
  }

  WiFiClientSecure client;
  // SECURITY NOTE: setInsecure() skips server certificate verification.
  // Replace with setCACert()/a verified CA bundle before relying on OTA.
  client.setInsecure();
  HTTPClient https;
  https.setTimeout(12000);
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!https.begin(client, GITHUB_RELEASE_API)) {
    otaStatusMessage = "HTTPS init failed";
    cleanupOTAWiFi(); otaBusy = false; resetScreenCache(); return false;
  }
  https.addHeader("User-Agent", GITHUB_USER_AGENT);
  https.addHeader("Accept", "application/vnd.github+json");
  int code = https.GET();
  if (code != HTTP_CODE_OK) {
    otaStatusMessage = "GitHub HTTP " + String(code);
    https.end(); cleanupOTAWiFi(); otaBusy = false; resetScreenCache(); return false;
  }
  String body = https.getString();
  https.end();

  String tag = extractJsonString(body, "tag_name");
  String htmlUrl = extractJsonString(body, "html_url");
  int assetsPos = body.indexOf("\"assets\"");
  String assetUrl = "";
  if (assetsPos >= 0) {
    int searchAt = assetsPos;
    while (true) {
      int urlPos = body.indexOf("\"browser_download_url\"", searchAt);
      if (urlPos < 0) break;
      String candidate = extractJsonString(body, "browser_download_url", urlPos);
      if (candidate.endsWith(".bin")) { assetUrl = candidate; break; }
      searchAt = urlPos + 22;
    }
  }
  cleanupOTAWiFi();
  otaBusy = false;

  if (tag.length() == 0 || assetUrl.length() == 0) {
    otaReleaseFound = false;
    otaStatusMessage = "No release .bin asset";
    otaFirmwareUrl = "";
    otaLatestVersion = tag.length() ? tag : "Unknown";
    resetScreenCache();
    return false;
  }
  otaLatestVersion = tag;
  otaFirmwareUrl = assetUrl;
  otaReleaseFound = true;
  otaStatusMessage = (tag == String(ZORA_FIRMWARE_VERSION) || tag == String("v") + ZORA_FIRMWARE_VERSION)
      ? "Already latest" : "Update available";
  Serial.println("[OTA] Release: " + otaLatestVersion);
  Serial.println("[OTA] Firmware URL: " + otaFirmwareUrl);
  resetScreenCache();
  return true;
}

void installLatestFirmware() {
  if (!otaReleaseFound || otaFirmwareUrl.length() == 0) {
    otaStatusMessage = "Check release first";
    resetScreenCache();
    return;
  }
  if (otaLatestVersion == String(ZORA_FIRMWARE_VERSION) ||
      otaLatestVersion == String("v") + ZORA_FIRMWARE_VERSION) {
    otaStatusMessage = "Already latest";
    resetScreenCache();
    return;
  }
  otaBusy = true;
  otaStatusMessage = "Connecting WiFi...";
  resetScreenCache();
  if (!connectWiFiForOTA()) {
    cleanupOTAWiFi(); otaBusy = false; resetScreenCache(); return;
  }

  WiFiClientSecure client;
  // SECURITY NOTE: certificate validation is disabled here; see code notes.
  client.setInsecure();
  HTTPClient https;
  https.setTimeout(20000);
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!https.begin(client, otaFirmwareUrl)) {
    otaStatusMessage = "Firmware HTTPS failed";
    cleanupOTAWiFi(); otaBusy = false; resetScreenCache(); return;
  }
  https.addHeader("User-Agent", GITHUB_USER_AGENT);
  int code = https.GET();
  if (code != HTTP_CODE_OK) {
    otaStatusMessage = "Download HTTP " + String(code);
    https.end(); cleanupOTAWiFi(); otaBusy = false; resetScreenCache(); return;
  }
  int contentLength = https.getSize();
  if (contentLength <= 0 || !Update.begin(contentLength)) {
    otaStatusMessage = "Invalid size/OTA space";
    https.end(); cleanupOTAWiFi(); otaBusy = false; resetScreenCache(); return;
  }

  otaStatusMessage = "Downloading...";
  resetScreenCache();
  WiFiClient* stream = https.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  bool sizeOK = (written == (size_t)contentLength);
  bool updateOK = sizeOK && Update.end() && Update.isFinished();
  https.end();
  cleanupOTAWiFi();
  otaBusy = false;

  if (updateOK) {
    otaStatusMessage = "Update done; rebooting";
    resetScreenCache();
    display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
    display.setCursor(0, 18); display.println("OTA SUCCESS");
    display.setCursor(0, 34); display.println("Restarting ZORA..."); display.display();
    delay(900);
    ESP.restart();
  } else {
    Update.abort();
    otaStatusMessage = "OTA failed; retry";
    Serial.printf("[OTA] written=%u expected=%d error=%u\n", (unsigned)written, contentLength, Update.getError());
    resetScreenCache();
  }
}

void drawOTAScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("FIRMWARE UPDATE");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setCursor(0, 14); display.print("Current: "); display.println(ZORA_FIRMWARE_VERSION);
  display.setCursor(0, 25); display.print("Latest:  "); display.println(otaLatestVersion);
  display.setCursor(0, 38); display.println(otaStatusMessage.substring(0, 21));
  display.setCursor(0, 50); display.println("1:Check  2:Install");
  display.setCursor(0, 59); display.println("3+ : Back");
  display.display();
}


// ============================================================
// MANUAL WI-FI CONTROL (OTA ONLY)
// ============================================================
bool connectWiFiManual() {
  if (WiFi.status() == WL_CONNECTED) {
    otaStatusMessage = "WiFi connected";
    return true;
  }

  wifiManualConnecting = true;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("OTA WI-FI");
  display.setCursor(0, 28);
  display.println("Connecting...");
  display.setCursor(0, 44);
  display.println("Please wait");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long started = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - started < WIFI_CONNECT_TIMEOUT_MS) {
    updateBuzzer();
    delay(20);
  }

  wifiManualConnecting = false;
  if (WiFi.status() == WL_CONNECTED) {
    otaStatusMessage = "WiFi connected";
    playConfirmSound();
    Serial.print("[WiFi] Connected, IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  otaStatusMessage = "WiFi connect failed";
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("WI-FI FAILED");
  display.setCursor(0, 28);
  display.println("Check SSID/password");
  display.setCursor(0, 44);
  display.println("Tap to continue");
  display.display();
  playErrorSound();
  delay(1200);
  return false;
}

void toggleManualWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    otaStatusMessage = "WiFi turned OFF";
    playBackSound();
    Serial.println("[WiFi] Manually disabled");
  } else {
    connectWiFiManual();
  }
  resetScreenCache();
}

// ============================================================
// SETTINGS SELECTION
// ============================================================

void selectSetting() {
  switch (settingsIndex) {
    case 0:
      currentMode = MODE_VOLUME;
      resetScreenCache();
      playConfirmSound();
      break;

    case 1:
      toggleChronosBLE();
      break;

    case 2:
      currentMode = MODE_NOTIFICATION;
      resetScreenCache();
      playSelectSound();
      break;

    case 3:
      startDinoGame();
      break;

    case 4:
      toggleManualWiFi();
      break;

    case 5:
      currentMode = MODE_OTA;
      otaStatusMessage = "Tap 1x to check";
      resetScreenCache();
      playConfirmSound();
      break;

    case 6:
      returnHome();
      break;
  }
}

// ============================================================
// CHRONOS BLE CALLBACKS
// ============================================================

void onChronosConnection(bool connected) {
  bleConnected = bleEnabled && connected;

  Serial.println(
    bleConnected ? "[BLE] Connected" : "[BLE] Disconnected"
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

      } else if (count == 5) {
        currentMode = MODE_NOTIFICATION;
        resetScreenCache();
        playSelectSound();

      } else if (count >= 6) {
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

    case MODE_DINO:
      dinoGameAction();
      break;

    case MODE_OTA:
      if (count == 1) {
        checkLatestGitHubRelease();
      } else if (count == 2) {
        installLatestFirmware();
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
  if (currentMode == MODE_HOME) {
    // Long touch only changes the expression.
    // It does NOT open Settings.
    happyActive = false;
    roboEyes.setMood(ANGRY);
    roboEyes.setPosition(DEFAULT);
    playErrorSound();

    Serial.println("[ZORA] Angry expression");
    return;
  }

  if (currentMode == MODE_DINO) {
    returnHome();
    return;
  }

  returnHome();
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
        roboEyes.setPosition(random(0, 2) == 0 ? ZORA_EYE_POSITION_EAST : ZORA_EYE_POSITION_WEST);
        happyActive = true;
        happyStartedAt = now;
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
  if (!bleEnabled || !bleConnected) {
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
          lastDrawnBle != bleEnabled) {
        drawSettings();
        lastDrawnSettingsIndex = settingsIndex;
        lastDrawnBle = bleEnabled;
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

    case MODE_DINO:
      updateDinoGame();
      drawDinoGame();
      break;

    case MODE_OTA:
      if (lastDrawnMode != MODE_OTA) drawOTAScreen();
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
  WiFi.mode(WIFI_OFF);  // Wi-Fi remains OFF until manually enabled in Settings.
  showBootSplash();

  roboEyes.begin(OLED_WIDTH, OLED_HEIGHT, 100);
  roboEyes.setAutoblinker(ON, 3, 2);
  roboEyes.setIdleMode(ON, 2, 2);
  roboEyes.setMood(DEFAULT);
  roboEyes.setPosition(DEFAULT);

  chronosWatch.setConnectionCallback(onChronosConnection);
  chronosWatch.setNotificationCallback(onChronosNotification);
  chronosWatch.setConfigurationCallback(onChronosConfiguration);

  // IMPORTANT:
  // Do NOT call chronosWatch.begin() here.
  // Chronos BLE starts only when enabled in Settings.
  bleEnabled = false;
  bleConnected = false;

  randomSeed(micros());
  resetScreenCache();

  Serial.println("[ZORA] Firmware: " ZORA_FIRMWARE_VERSION);
  Serial.println("[ZORA] Ready");
  Serial.println("[ZORA] Chronos BLE default: OFF");
  Serial.println("[ZORA] Serial Monitor: 115200 baud");
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
  updateTouch();

  // Return from HAPPY to the normal idle expression after five seconds.
  if (happyActive && currentMode == MODE_HOME &&
      millis() - happyStartedAt >= HAPPY_DURATION_MS) {
    happyActive = false;
    roboEyes.setMood(DEFAULT);
    roboEyes.setPosition(DEFAULT);
  }

  // Chronos loop runs only after manual BLE activation.
  if (bleEnabled) {
    chronosWatch.loop();
  }

  updateBuzzer();
  updateNavigationOverride();

  renderCurrentScreen();

  delay(1);
}
