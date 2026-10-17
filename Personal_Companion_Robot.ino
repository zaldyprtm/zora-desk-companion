
#include <Arduino.h>
#include <Wire.h>
#include <esp_arduino_version.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FluxGarage_RoboEyes.h>
#include <ChronosESP32.h>

// =====================================================
// ESP32-C3 SUPER MINI - ZORA ROBO EYES + CHRONOS BLE (NO WIFI)
// =====================================================

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C

#define OLED_SDA      20
#define OLED_SCL      21
#define TOUCH_PIN      5
#define BUZZER_PIN     6

// Timing
const unsigned long TOUCH_DEBOUNCE_MS = 40;
const unsigned long TAP_TIMEOUT_MS = 700;
const unsigned long LONG_TOUCH_MS = 900;
const unsigned long ANGRY_DISPLAY_MS = 1000;
const unsigned long CRY_DISPLAY_MS = 2800;
const unsigned long EMOTION_FRAME_MS = 100;
const unsigned long CLOCK_REFRESH_MS = 500;

// Passive buzzer: nada startup + suara sentuh interaktif
const uint16_t STARTUP_NOTES[] = {523, 659, 784, 1047, 784, 880, 1047, 1319};
const uint16_t STARTUP_DURATIONS[] = {150, 150, 150, 260, 130, 150, 150, 420};
const uint8_t STARTUP_NOTE_COUNT = sizeof(STARTUP_NOTES) / sizeof(STARTUP_NOTES[0]);

// Untuk Arduino-ESP32 core 2.x
const int BUZZER_CHANNEL = 0;

// =====================================================
// OBJECTS
// =====================================================

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

RoboEyes<Adafruit_SSD1306> roboEyes(display);
ChronosESP32 chronosWatch("ZORA");
bool chronosTimeSynced = false;
bool chronosConnected = false;

// =====================================================
// SCREEN MODE
// =====================================================

enum ScreenMode {
  MODE_EYES,
  MODE_CLOCK,
  MODE_WEATHER,
  MODE_NOTIFICATION,
  MODE_NAVIGATION
};

ScreenMode screenMode = MODE_EYES;

// =====================================================
// STATE
// =====================================================

bool touchState = false;
bool lastRawTouch = false;
bool moveNE = true;
bool buzzerActive = false;
uint16_t soundNotes[4] = {0, 0, 0, 0};
uint16_t soundDurations[4] = {0, 0, 0, 0};
uint8_t soundNoteCount = 0;
uint8_t soundNoteIndex = 0;
unsigned long soundNoteStarted = 0;
bool suppressNextReleaseTap = false;
bool longTouchTriggered = false;
unsigned long pressStartedAt = 0;

enum EmotionMode { EMOTION_NONE, EMOTION_ANGRY, EMOTION_CRYING };
EmotionMode emotionMode = EMOTION_NONE;
unsigned long emotionUntil = 0;
unsigned long lastEmotionFrame = 0;
uint8_t emotionFrame = 0;

unsigned long lastRawChange = 0;
unsigned long lastTapTime = 0;
unsigned long lastClockDraw = 0;

uint8_t tapCount = 0;

// =====================================================
// CHRONOS WEATHER + NOTIFICATION STATE
// =====================================================

String latestNotifApp = "";
String latestNotifTitle = "";
String latestNotifMessage = "";
unsigned long notificationUntil = 0;
unsigned long lastNavigationDraw = 0;
bool navigationWasActive = false;

// =====================================================
// PASSIVE BUZZER - LEDC PWM
// Support Arduino-ESP32 core 2.x and 3.x
// =====================================================

bool initBuzzer() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return ledcAttach(BUZZER_PIN, 2000, 8);
#else
  ledcSetup(BUZZER_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  return true;
#endif
}

void buzzerTone(uint32_t frequency) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(BUZZER_PIN, frequency);
#else
  ledcWriteTone(BUZZER_CHANNEL, frequency);
#endif
}

void playSoundSequence(const uint16_t* notes,
                       const uint16_t* durations,
                       uint8_t count) {
  if (count == 0 || count > 4) return;

  soundNoteCount = count;
  soundNoteIndex = 0;

  for (uint8_t i = 0; i < count; i++) {
    soundNotes[i] = notes[i];
    soundDurations[i] = durations[i];
  }

  buzzerTone(soundNotes[0]);
  soundNoteStarted = millis();
  buzzerActive = true;
}

void playTouchSound(uint8_t tapNumber) {
  static const uint16_t note1[] = {880, 1175};
  static const uint16_t dur1[]  = {55, 85};
  static const uint16_t note2[] = {784, 988, 1319};
  static const uint16_t dur2[]  = {45, 45, 80};
  static const uint16_t note3[] = {523, 784, 1047};
  static const uint16_t dur3[]  = {65, 65, 120};
  static const uint16_t note4[] = {1047, 784, 523};
  static const uint16_t dur4[]  = {55, 55, 140};

  switch (tapNumber) {
    case 2: playSoundSequence(note2, dur2, 3); break;
    case 3: playSoundSequence(note3, dur3, 3); break;
    case 4: playSoundSequence(note4, dur4, 3); break;
    default: playSoundSequence(note1, dur1, 2); break;
  }
}

void updateBuzzer() {
  if (!buzzerActive) return;

  if (millis() - soundNoteStarted >= soundDurations[soundNoteIndex]) {
    soundNoteIndex++;

    if (soundNoteIndex >= soundNoteCount) {
      buzzerTone(0);
      buzzerActive = false;
      soundNoteCount = 0;
      return;
    }

    buzzerTone(soundNotes[soundNoteIndex]);
    soundNoteStarted = millis();
  }
}

// Gambar splash ZORA sambil memainkan melodi welcome.
// Dipanggil setelah OLED berhasil diinisialisasi.
void drawBootFrame(uint8_t progress, uint8_t frame) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Bingkai dan aksen sudut
  display.drawRoundRect(2, 2, 124, 60, 7, SSD1306_WHITE);
  display.drawLine(10, 8, 23, 8, SSD1306_WHITE);
  display.drawLine(105, 55, 118, 55, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(31, 9);
  display.print("ZORA");

  display.setTextSize(1);
  display.setCursor(26, 29);
  display.print("SMART COMPANION");

  // Animasi titik bergerak
  for (uint8_t i = 0; i < 3; i++) {
    int x = 49 + i * 15;
    bool active = ((frame + i) % 3) == 0;
    if (active) display.fillCircle(x, 42, 2, SSD1306_WHITE);
    else display.drawCircle(x, 42, 2, SSD1306_WHITE);
  }

  display.drawRoundRect(18, 49, 92, 7, 3, SSD1306_WHITE);
  int fillWidth = (88 * progress) / 100;
  if (fillWidth > 0) display.fillRoundRect(20, 51, fillWidth, 3, 1, SSD1306_WHITE);

  display.display();
}

void bootAnimationAndWelcome() {
  Serial.println("ZORA boot animation + welcome melody...");

  uint8_t frame = 0;
  uint8_t progress = 0;

  for (uint8_t i = 0; i < STARTUP_NOTE_COUNT; i++) {
    buzzerTone(STARTUP_NOTES[i]);
    unsigned long noteStart = millis();
    while (millis() - noteStart < STARTUP_DURATIONS[i]) {
      progress = (uint8_t)min(99UL,
        ((unsigned long)(i * 100) / STARTUP_NOTE_COUNT) +
        ((millis() - noteStart) * 100UL /
         (STARTUP_NOTE_COUNT * STARTUP_DURATIONS[i])));
      drawBootFrame(progress, frame++);
      delay(35);
    }

    buzzerTone(0);
    delay(25);
  }

  buzzerActive = false;
  buzzerTone(0);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.drawRoundRect(2, 2, 124, 60, 7, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(31, 10);
  display.print("ZORA");
  display.setTextSize(1);
  display.setCursor(22, 34);
  display.print("WELCOME ONLINE");
  display.setCursor(31, 47);
  display.print("Let's grow!");
  display.display();
  delay(550);

  Serial.println("Welcome melody selesai.");
}

void drawWeather();
void drawNotification();
void drawNavigation();

// =====================================================
// CHRONOS BLE - TIME, WEATHER, NOTIFICATIONS
// Tidak memakai Wi-Fi / OpenWeather di ESP32-C3.
// Data jam/cuaca/notifikasi dikirim aplikasi Chronos lewat BLE.
// =====================================================

void chronosConnectionCallback(bool state) {
  chronosConnected = state;
  Serial.println(state ? "Chronos BLE terhubung" : "Chronos BLE terputus");
}

void chronosNotificationCallback(Notification notification) {
  latestNotifApp = notification.app;
  latestNotifTitle = notification.title;
  latestNotifMessage = notification.message;
  notificationUntil = millis() + 5000UL;
  screenMode = MODE_NOTIFICATION;
  drawNotification();

  Serial.println("Notifikasi Chronos diterima:");
  Serial.println(latestNotifApp);
  Serial.println(latestNotifTitle);
  Serial.println(latestNotifMessage);
}

void chronosConfigCallback(Config config, uint32_t a, uint32_t b) {
  (void)b;

  if (config == CF_TIME) {
    // ChronosESP32 melaporkan sinkronisasi waktu lewat callback ini.
    if (a == 0) {
      chronosTimeSynced = true;
      Serial.print("Waktu dari Chronos: ");
      Serial.println(chronosWatch.getTimeDate());
    } else {
      Serial.println("Chronos mengirim info waktu...");
    }
  } else if (config == CF_HR24) {
    Serial.println("Pengaturan format jam diterima dari Chronos");
  } else if (config == CF_NAV_DATA) {
    Serial.print("Status navigasi Chronos berubah; active=");
    Serial.println(a ? "true" : "false");
  } else if (config == CF_NAV_ICON) {
    Serial.println("Data ikon navigasi Chronos diterima");
  } else if (config == CF_WEATHER) {
    Serial.println("Pembaruan cuaca dari Chronos diterima");
    Serial.print("Kota: ");
    Serial.println(chronosWatch.getWeatherCity());
    Serial.print("Jumlah data cuaca: ");
    Serial.println(chronosWatch.getWeatherCount());
    if (screenMode == MODE_WEATHER) {
      drawWeather();
    }
  }
}

// =====================================================
// CLOCK SCREEN
// =====================================================

void drawClock() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("ZORA | JAM WIB");
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  if (chronosTimeSynced) {
    String timeText = chronosWatch.getTime("%H:%M:%S");
    String dateText = chronosWatch.getTime("%d-%m-%Y");

    display.setTextSize(2);
    display.setCursor(8, 20);
    display.println(timeText);

    display.setTextSize(1);
    display.setCursor(30, 45);
    display.println(dateText);
  } else {
    display.setCursor(4, 23);
    display.println("Menunggu sync BLE...");
    display.setCursor(4, 36);
    display.println(chronosConnected ? "Chronos terhubung" : "Buka app Chronos");
  }

  display.setCursor(0, 56);
  display.println(chronosConnected ? "BLE: CONNECTED" : "BLE: WAITING");

  display.display();
}

// =====================================================
// WEATHER SCREEN
// =====================================================

const char* weatherIconName(int icon) {
  switch (icon) {
    case 0: return "Berawan";
    case 1: return "Cerah";
    case 2: return "Salju";
    case 3: return "Hujan";
    case 4: return "Mendung";
    case 5: return "Ekstrem";
    case 6: return "Berangin";
    case 7: return "Kabut";
    default: return "Cuaca";
  }
}

void drawWeather() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("CHRONOS WEATHER");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  int count = chronosWatch.getWeatherCount();
  if (!chronosConnected || count <= 0) {
    display.setCursor(0, 19);
    display.println("Cuaca belum diterima");
    display.setCursor(0, 32);
    display.println("Buka aplikasi Chronos");
    display.setCursor(0, 44);
    display.println("aktifkan/sinkron cuaca");
    display.setCursor(0, 56);
    display.println("BLE only - tanpa WiFi");
    display.display();
    return;
  }

  String city = chronosWatch.getWeatherCity();
  display.setCursor(0, 12);
  display.print(city.substring(0, 20));

  Weather today = chronosWatch.getWeatherAt(0);
  display.setTextSize(2);
  display.setCursor(0, 22);
  display.print(today.temp);
  display.print(" C");

  display.setTextSize(1);
  display.setCursor(72, 24);
  display.print(weatherIconName(today.icon));

  display.setCursor(0, 43);
  display.print("H:");
  display.print(today.high);
  display.print("C L:");
  display.print(today.low);
  display.print("C");

  display.setCursor(0, 55);
  display.print("UV:");
  display.print(today.uv);
  display.print(" P:");
  display.print(today.pressure);
  display.print("hPa");

  display.display();
}

void drawNotification() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("ZORA | NOTIFIKASI");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 13);
  display.print(latestNotifApp.substring(0, 21));

  display.setCursor(0, 24);
  display.print(latestNotifTitle.substring(0, 21));

  display.setCursor(0, 36);
  display.print(latestNotifMessage.substring(0, 21));
  if (latestNotifMessage.length() > 21) {
    display.setCursor(0, 46);
    display.print(latestNotifMessage.substring(21, 42));
  }

  display.setCursor(0, 56);
  display.print("Chronos BLE");
  display.display();
}

// =====================================================
// NAVIGATION SCREEN - Google Maps -> Chronos BLE -> ZORA
// Tidak memakai Wi-Fi. Data navigasi dibaca dari library ChronosESP32.
// =====================================================
void drawNavigation() {
  Navigation &nav = chronosWatch.getNavigation();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ZORA NAV");
  display.setCursor(75, 0);
  display.print("BLE");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  if (!nav.active || !nav.isNavigation) {
    display.setCursor(3, 23);
    display.print("Menunggu Google Maps");
    display.setCursor(3, 37);
    display.print("Mulai rute di HP");
    display.display();
    return;
  }

  // Chronos mengirim ikon 48x48, 1 bit per pixel.
  if (nav.hasIcon) {
    display.drawBitmap(0, 14, nav.icon, 48, 48, SSD1306_WHITE);
  } else {
    // Panah sederhana sebagai fallback jika ikon belum tersedia.
    String instruction = nav.directions;
    instruction.toLowerCase();
    int cx = 24;
    if (instruction.indexOf("left") >= 0 || instruction.indexOf("kiri") >= 0) {
      display.drawLine(cx + 12, 42, cx - 8, 42, SSD1306_WHITE);
      display.drawLine(cx - 8, 42, cx + 1, 33, SSD1306_WHITE);
      display.drawLine(cx - 8, 42, cx + 1, 51, SSD1306_WHITE);
    } else if (instruction.indexOf("right") >= 0 || instruction.indexOf("kanan") >= 0) {
      display.drawLine(cx - 12, 42, cx + 8, 42, SSD1306_WHITE);
      display.drawLine(cx + 8, 42, cx - 1, 33, SSD1306_WHITE);
      display.drawLine(cx + 8, 42, cx - 1, 51, SSD1306_WHITE);
    } else {
      display.drawLine(cx, 55, cx, 25, SSD1306_WHITE);
      display.drawLine(cx, 25, cx - 9, 34, SSD1306_WHITE);
      display.drawLine(cx, 25, cx + 9, 34, SSD1306_WHITE);
    }
  }

  // Area kanan: jarak, instruksi, dan ETA jika tersedia.
  display.setCursor(52, 15);
  if (nav.distance.length()) display.print(nav.distance.substring(0, 12));
  else display.print("NAVIGASI");

  display.setCursor(52, 29);
  if (nav.directions.length()) display.print(nav.directions.substring(0, 12));
  else if (nav.title.length()) display.print(nav.title.substring(0, 12));
  else display.print("Ikuti rute");

  display.setCursor(52, 43);
  if (nav.eta.length()) {
    display.print("ETA ");
    display.print(nav.eta.substring(0, 7));
  } else if (nav.duration.length()) {
    display.print(nav.duration.substring(0, 10));
  }

  display.setCursor(52, 56);
  display.print("MAPS / BLE");
  display.display();
}

// =====================================================
// SCREEN CONTROL
// =====================================================

// =====================================================
// ZORA EMOTIONS: ANGRY + CRYING
// =====================================================

void playAngrySound() {
  static const uint16_t notes[] = {330, 262, 196, 147};
  static const uint16_t durations[] = {90, 100, 130, 180};
  playSoundSequence(notes, durations, 4);
}

void playCryingSound() {
  static const uint16_t notes[] = {988, 784, 659, 523};
  static const uint16_t durations[] = {110, 150, 180, 260};
  playSoundSequence(notes, durations, 4);
}

void drawAngryFace(uint8_t frame) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(4, 3); display.print("ZORA");
  display.setCursor(83, 3); display.print("ANGRY!");
  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);

  int lift = (frame % 2) ? 1 : 0;
  display.drawLine(20, 25, 47, 19 + lift, SSD1306_WHITE);
  display.drawLine(81, 19 + lift, 108, 25, SSD1306_WHITE);
  display.fillRoundRect(22, 27 + lift, 28, 13, 5, SSD1306_WHITE);
  display.fillRoundRect(78, 27 + lift, 28, 13, 5, SSD1306_WHITE);
  display.fillCircle(36, 33 + lift, 4, SSD1306_BLACK);
  display.fillCircle(92, 33 + lift, 4, SSD1306_BLACK);
  display.drawLine(47, 51, 55, 46, SSD1306_WHITE);
  display.drawLine(55, 46, 73, 46, SSD1306_WHITE);
  display.drawLine(73, 46, 81, 51, SSD1306_WHITE);
  display.setCursor(40, 56); display.print("HMPH!");
  display.display();
}

void drawCryingFace(uint8_t frame) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(4, 3); display.print("ZORA");
  display.setCursor(78, 3); display.print("SAD MODE");
  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);

  display.drawCircle(37, 31, 10, SSD1306_WHITE);
  display.drawCircle(91, 31, 10, SSD1306_WHITE);
  display.fillCircle(37, 33, 4, SSD1306_WHITE);
  display.fillCircle(91, 33, 4, SSD1306_WHITE);
  display.drawLine(23, 18, 34, 22, SSD1306_WHITE);
  display.drawLine(34, 22, 47, 18, SSD1306_WHITE);
  display.drawLine(81, 18, 94, 22, SSD1306_WHITE);
  display.drawLine(94, 22, 105, 18, SSD1306_WHITE);

  int tear1 = 38 + (frame % 4) * 3;
  int tear2 = 38 + ((frame + 2) % 4) * 3;
  display.fillCircle(25, tear1, 2, SSD1306_WHITE);
  display.fillCircle(103, tear2, 2, SSD1306_WHITE);
  display.drawRoundRect(57, 43, 14, 9, 4, SSD1306_WHITE);
  display.setCursor(42, 56); display.print("DON'T GO...");
  display.display();
}

void startEmotion(EmotionMode mode, unsigned long duration) {
  emotionMode = mode;
  emotionUntil = millis() + duration;
  lastEmotionFrame = 0;
  emotionFrame = 0;
  if (mode == EMOTION_ANGRY) playAngrySound();
  else if (mode == EMOTION_CRYING) playCryingSound();
}

void updateEmotion() {
  if (emotionMode == EMOTION_NONE) return;
  unsigned long now = millis();

  bool angryStillHeld = (emotionMode == EMOTION_ANGRY &&
                         touchState && longTouchTriggered);
  if (!angryStillHeld && (long)(now - emotionUntil) >= 0) {
    emotionMode = EMOTION_NONE;
    roboEyes.setMood(DEFAULT);
    roboEyes.setPosition(DEFAULT);
    roboEyes.setAutoblinker(ON, 3, 2);
    roboEyes.setIdleMode(ON, 2, 2);
    return;
  }

  if (now - lastEmotionFrame >= EMOTION_FRAME_MS) {
    lastEmotionFrame = now;
    emotionFrame++;
    if (emotionMode == EMOTION_ANGRY) drawAngryFace(emotionFrame);
    else if (emotionMode == EMOTION_CRYING) drawCryingFace(emotionFrame);
  }
}

void returnToEyes() {
  screenMode = MODE_EYES;

  roboEyes.setMood(DEFAULT);
  roboEyes.setPosition(DEFAULT);
  roboEyes.setAutoblinker(ON, 3, 2);
  roboEyes.setIdleMode(ON, 2, 2);
}

void enterScreen(ScreenMode newMode) {
  screenMode = newMode;

  if (screenMode == MODE_CLOCK) {
    lastClockDraw = 0;
    drawClock();
  }

  if (screenMode == MODE_WEATHER) {
    drawWeather();
  }
}

// =====================================================
// TAP SEQUENCE
// 3 taps = clock
// 4 taps = forecast
// =====================================================

void handleTapSequence() {
  uint8_t completedTaps = tapCount;
  tapCount = 0;
  if (completedTaps == 3) enterScreen(MODE_CLOCK);
  else if (completedTaps == 4) enterScreen(MODE_WEATHER);
  else if (completedTaps == 5) startEmotion(EMOTION_CRYING, CRY_DISPLAY_MS);
}

// =====================================================
// TOUCH HANDLER
// =====================================================

void updateTouch() {
  unsigned long now = millis();
  bool rawTouch = digitalRead(TOUCH_PIN) == HIGH;

  if (rawTouch != lastRawTouch) {
    lastRawTouch = rawTouch;
    lastRawChange = now;
  }
  if (now - lastRawChange < TOUCH_DEBOUNCE_MS) return;

  // Detect a hold while touch remains HIGH.
  if (rawTouch && touchState && screenMode == MODE_EYES &&
      !longTouchTriggered && now - pressStartedAt >= LONG_TOUCH_MS) {
    longTouchTriggered = true;
    tapCount = 0;
    startEmotion(EMOTION_ANGRY, LONG_TOUCH_MS + ANGRY_DISPLAY_MS);
  }
  if (rawTouch == touchState) return;

  touchState = rawTouch;
  if (touchState) {
    pressStartedAt = now;
    longTouchTriggered = false;

    if (emotionMode == EMOTION_CRYING) {
      suppressNextReleaseTap = true;
      return;
    }
    // Pertahankan layar navigasi selama Google Maps masih mengirim rute.
    if (screenMode == MODE_NAVIGATION) {
      suppressNextReleaseTap = true;
      return;
    }
    if (screenMode != MODE_EYES) {
      returnToEyes();
      suppressNextReleaseTap = true;
      playTouchSound(1);
      return;
    }

    roboEyes.setMood(HAPPY);
    roboEyes.setAutoblinker(OFF);
    roboEyes.setIdleMode(OFF);
    if (moveNE) roboEyes.setPosition(NE);
    else roboEyes.setPosition(NW);
    moveNE = !moveNE;
    playTouchSound((tapCount < 4) ? (tapCount + 1) : 1);
  } else {
    if (suppressNextReleaseTap) {
      suppressNextReleaseTap = false;
      longTouchTriggered = false;
      return;
    }
    if (longTouchTriggered) {
      emotionUntil = millis() + ANGRY_DISPLAY_MS;
      longTouchTriggered = false;
      return;
    }
    if (screenMode == MODE_EYES && emotionMode != EMOTION_CRYING) {
      roboEyes.setMood(DEFAULT);
      roboEyes.setAutoblinker(ON, 3, 2);
      roboEyes.setIdleMode(ON, 2, 2);
      if (tapCount < 5) tapCount++;
      lastTapTime = now;
    }
  }
}

// Baca state navigasi yang diperbarui oleh ChronosESP32 saat data BLE masuk.
// Navigasi otomatis mengambil alih OLED, lalu kembali ke RoboEyes saat rute selesai.
void updateNavigationMode() {
  Navigation &nav = chronosWatch.getNavigation();
  bool activeNow = nav.active && nav.isNavigation;

  if (activeNow) {
    if (!navigationWasActive || screenMode != MODE_NAVIGATION) {
      screenMode = MODE_NAVIGATION;
      lastNavigationDraw = 0;
      Serial.println("Navigasi aktif: OLED masuk mode ZORA NAV");
    }
    navigationWasActive = true;

    if (millis() - lastNavigationDraw >= 250UL) {
      lastNavigationDraw = millis();
      drawNavigation();
    }
  } else if (navigationWasActive) {
    navigationWasActive = false;
    Serial.println("Navigasi selesai: kembali ke RoboEyes");
    returnToEyes();
  }
}

// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  // Inisialisasi buzzer lebih dulu, tetapi melodi dimainkan
  // setelah OLED siap agar animasi boot bisa tampil bersamaan.
  bool buzzerReady = initBuzzer();
  if (!buzzerReady) {
    Serial.println("ERROR: LEDC buzzer gagal diinisialisasi!");
  }

  pinMode(TOUCH_PIN, INPUT);

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDR
      )) {
    Serial.println("ERROR: OLED tidak terdeteksi!");

    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.display();

  if (buzzerReady) {
    delay(50);
    bootAnimationAndWelcome();
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(31, 10);
    display.print("ZORA");
    display.setTextSize(1);
    display.setCursor(10, 35);
    display.print("BUZZER NOT READY");
    display.display();
    delay(700);
  }

  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);
  roboEyes.setPosition(DEFAULT);
  roboEyes.setMood(DEFAULT);
  roboEyes.setAutoblinker(ON, 3, 2);
  roboEyes.setIdleMode(ON, 2, 2);

  // Chronos BLE only: Wi-Fi dan OpenWeather sengaja tidak dipakai.
  chronosWatch.setConnectionCallback(chronosConnectionCallback);
  chronosWatch.setNotificationCallback(chronosNotificationCallback);
  chronosWatch.setConfigurationCallback(chronosConfigCallback);
  chronosWatch.set24Hour(true);
  chronosWatch.begin();
  Serial.print("Nama BLE ZORA: ZORA | Alamat: ");
  Serial.println(chronosWatch.getAddress());

  Serial.println("RoboEyes + Chronos BLE Clock/Weather/Notifications/Navigation siap (tanpa Wi-Fi)!");
  Serial.println("3 tap = jam | 4 tap = cuaca | 5 tap = menangis | tahan 900ms = marah | Maps aktif = navigasi OLED");
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  chronosWatch.loop();  // wajib dipanggil terus untuk BLE dan sinkronisasi waktu
  updateNavigationMode();
  updateTouch();
  updateBuzzer();

  // 3 tap = jam, 4 tap = cuaca, 5 tap = menangis.
  if (screenMode == MODE_EYES && emotionMode == EMOTION_NONE &&
      tapCount > 0 && millis() - lastTapTime >= TAP_TIMEOUT_MS) {
    handleTapSequence();
  }

  if (emotionMode != EMOTION_NONE) {
    updateEmotion();
    return;
  }

  if (screenMode == MODE_NAVIGATION) {
    // Layar navigasi digambar periodik oleh updateNavigationMode().
  } else if (screenMode == MODE_NOTIFICATION) {
    if ((long)(millis() - notificationUntil) >= 0) {
      returnToEyes();
    } else {
      // Callback draws immediately; keep this mode until timeout.
    }
  } else if (screenMode == MODE_CLOCK) {
    if (millis() - lastClockDraw >= CLOCK_REFRESH_MS) {
      lastClockDraw = millis();
      drawClock();
    }
  } else if (screenMode == MODE_WEATHER) {
    // Cuaca dibaca dari cache Chronos; tidak ada request internet dari ESP32.
    static unsigned long lastWeatherDraw = 0;
    if (millis() - lastWeatherDraw >= 2000UL) {
      lastWeatherDraw = millis();
      drawWeather();
    }
  } else {
    roboEyes.update();
  }
}
