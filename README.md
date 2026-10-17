ZORA v1.0 --- Companion Desk
ZORA is a compact ESP32-C3 companion device built around an OLED
display, expressive RoboEyes animations, and Bluetooth Low Energy (BLE)
integration through the Chronos Android app. It is designed to bring
useful phone information to a small desk companion while keeping the
firmware lightweight and avoiding a dedicated Wi-Fi connection.
Current communication model: Chronos app ↔ BLE ↔ ZORA. The ESP32
does not use Wi-Fi or make direct OpenWeather API requests in this
firmware.

Features
- Chronos BLE connection for supported phone data.
- Clock and date synchronized through Chronos.
- Weather screen using weather data provided by Chronos.
- Phone notifications displayed on the OLED when forwarded by
  Chronos.
- Google Maps turn-by-turn navigation forwarded by Chronos over
  BLE, including navigation instruction and available
  direction/distance/ETA details.
- RoboEyes expressions with idle animation and touch-triggered
  emotion effects.
- OLED boot animation and welcome melody using the display and
  buzzer.
- Touch controls for switching screens and triggering expressions.
- No Wi-Fi required for the features listed above.
Navigation display depends on the Chronos app forwarding navigation data
successfully and the required Android notification access being enabled.
The exact fields shown can depend on the navigation data supplied by the
app.
Hardware
- ESP32-C3 Super Mini
- 128 × 64 SSD1306 I²C OLED display (commonly address 0x3C)
- Touch sensor
- Buzzer
- Optional wiring and enclosure components for the final build
Current pin configuration
  Component        ESP32-C3 GPIO
  OLED SDA               GPIO 20
  OLED SCL               GPIO 21
  Touch sensor            GPIO 5
  Buzzer                  GPIO 6
Check the pin definitions in the sketch before wiring, especially if
your hardware revision differs.
Software and libraries
- Arduino IDE with an ESP32-C3 board package
- Adafruit GFX Library
- Adafruit SSD1306
- FluxGarage RoboEyes
- ChronosESP32
ChronosESP32 library: https://github.com/fbiego/chronos-esp32\
Chronos Android app:
https://play.google.com/store/apps/details?id=com.fbiego.chronos
Use a Chronos app version that supports navigation forwarding to
ChronosESP32 devices.
Setup
1. Install the ESP32 board support package and the libraries listed
   above in Arduino IDE.
2. Open ZORA_Chronos_BLE_Navigation.ino.
3. Select the appropriate ESP32-C3 board and serial port.
4. Verify the GPIO assignments against your wiring, then compile and
   upload.
5. Open Chronos on the Android phone and connect to the BLE device
   named ZORA from within the Chronos app.
6. Grant Chronos Notification access in Android settings so it can
   forward supported notifications and navigation events.
7. In Chronos, open ESP32 Manager and enable Navigation.
8. Start a route in Google Maps and check that navigation instructions
   appear on ZORA's OLED.
Android troubleshooting
If BLE shows Connected but navigation does not appear:
- Run the Chronos Self Test and confirm it reports OK.
- Re-check Android's Notification access for Chronos.
- On MIUI/HyperOS devices, set Chronos battery usage to No
  restrictions if that option is available.
- Force-close and reopen Chronos, reconnect ZORA inside the app, then
  test navigation again.
A successful BLE connection alone does not guarantee that Android is
forwarding navigation data.
Touch controls
The current sketch uses the following touch gestures:
  Gesture                             Action
  3 taps                              Clock screen
  4 taps                              Weather screen
  5 taps                              Crying expression
  Touch and hold for about 900 ms     Angry expression
  Google Maps navigation active and   Navigation screen can take over the
  received                            OLED
Gesture handling and navigation-screen priority are defined by the
firmware; check the sketch if you customize these behaviors.
Garden integration vision
ZORA is also part of a broader smart-garden project direction. Future
versions may communicate with other ESP-based garden devices, such as a
soil-monitoring or automatic-watering unit. That integration is a
development goal and is not implied to be active in the current BLE
companion firmware.
Notes and limitations
- ZORA receives supported data through Chronos BLE; it does not
  independently fetch Google Maps routes.
- Navigation requires Google Maps navigation to be running on the
  phone and Chronos to forward its navigation data.
- Weather availability depends on weather data being present in
  Chronos.
- Notification and navigation forwarding depend on Android permissions
  and background-app behavior.
- BLE range and reliability depend on the phone, environment, and
  power conditions.
License
Add a license file before publishing the repository if you want to
specify reuse, modification, and distribution terms.
