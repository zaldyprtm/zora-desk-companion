# ZORA — Desk Companion & Garden IoT Device

**Version:** 1.0  
**Project:** MZP | ZDYVOID  
**Platform:** ESP32-C3 Super Mini  
**Connectivity:** Bluetooth Low Energy (BLE) via Chronos

ZORA is a custom-built IoT desk companion designed to combine interactive expressions, timekeeping, and smartphone connectivity in a compact desktop device.

As part of the **MZP | ZDYVOID IoT ecosystem**, ZORA is envisioned to communicate with other ESP-based devices in the garden, including SmartPlant, while serving as an interactive desk companion.

ZORA v1.0 connects to the **Chronos app** through Bluetooth Low Energy (BLE), enabling supported time and information features without requiring Wi-Fi for Bluetooth-based communication.

> **Status:** v1.0 — Initial release  
> More features and integrations are planned for future updates.

---

## Features

- **Interactive RoboEyes** — Animated eyes for a more expressive desktop companion.
- **Digital Clock** — Display the time and date with 3 consecutive touches.
- **Weather Display** — Open the weather screen with 4 consecutive touches when supported weather data is available through Chronos.
- **Sad Animation** — Trigger a crying expression with 5 consecutive touches.
- **Angry Animation** — Hold the touch sensor to trigger an angry expression.
- **BLE Connectivity** — Connect to the Chronos smartphone app using Bluetooth Low Energy.
- **Compact Hardware Design** — Built around an ESP32-C3 Super Mini with an OLED display and a rechargeable battery system.
- **Future IoT Integration** — Designed with the long-term goal of communicating with other ESP-based devices in the garden.

## Hardware Components

| Component | Description |
|---|---|
| ESP32-C3 Super Mini | Main controller with Wi-Fi and BLE capabilities |
| 0.96-inch OLED | 128 × 64 display for RoboEyes, clock, and information |
| Passive Buzzer | Audio feedback and interactive sound effects |
| Small ON/OFF Switch | Main power control |
| TP4056 USB Type-C Charging Module | Battery charging module with protection |
| LiPo Battery | Rechargeable power source |
| TTP223 Touch Sensor | Touch-based user interaction |
| Custom Enclosure | Custom-designed case for the device |

## Touch Controls

| Input | Action |
|---|---|
| 3 consecutive touches | Open digital clock |
| 4 consecutive touches | Open weather display |
| 5 consecutive touches | Show sad animation |
| Long touch | Show angry animation |

*Touch sequences must be completed within the firmware's configured detection interval.*

## Connectivity

### Chronos BLE Integration

ZORA v1.0 uses Bluetooth Low Energy to communicate with the Chronos app on Android.

Supported functionality depends on the information and commands provided by the app and the installed firmware.

- Time synchronization through BLE.
- Weather information display when compatible data is received.
- No Wi-Fi connection is required for BLE-based features.

### Future Garden IoT Integration

ZORA is part of a broader vision to connect desktop companions and garden IoT devices.

Potential future integrations include:

- Communication with ESP8266-based Wemos D1 mini and ESP32-based SmartPlant devices.
- Sharing plant monitoring data and device status.
- Cross-device notifications and interactive status updates.
- A unified ecosystem for desktop companionship and smart gardening.

*Communication between ZORA and other garden devices is a planned integration, not a confirmed feature of v1.0.*

## Development Roadmap

- [x] Interactive RoboEyes
- [x] Touch-based expressions
- [x] Chronos BLE integration
- [x] Digital clock interface
- [ ] Extended weather information
- [ ] Communication with garden IoT devices
- [ ] SmartPlant status integration
- [ ] Additional expressions and animations
- [ ] Future firmware improvements

## Getting Started

1. Assemble the hardware according to the project's wiring configuration.
2. Install the required Arduino libraries for the ESP32-C3, OLED display, RoboEyes, and Chronos BLE integration.
3. Open the ZORA firmware in Arduino IDE.
4. Select the appropriate ESP32-C3 board and COM port.
5. Upload the firmware.
6. Open the Chronos app on Android and pair with ZORA through the app's device pairing interface.

Library requirements and setup instructions may vary between firmware versions.

## Project Information

**Project name:** ZORA  
**Version:** v1.0  
**Developer / Project identity:** MZP | ZDYVOID  
**Category:** Embedded Systems · IoT · BLE · Desk Companion · Smart Gardening

ZORA is an evolving personal IoT project exploring the connection between interactive desktop devices and smart garden systems.

**More features will be coming soon.**

---![Uploading WhatsApp Image 2026-10-17 at 12.25.12.jpeg…]()
<img width="960" height="1280" alt="WhatsApp Image 2026-10-17 at 12 25 12 (1)" src="https://github.com/user-attachments/assets/e0b44c6b-54eb-4c7c-ad59-5a800293dd15" />
<img width="899" height="1245" alt="WhatsApp Image 2026-10-17 at 12 24 21" src="https://github.com/user-attachments/assets/6ddc1694-d927-492a-81fe-4cd597a6a14c" />


**MZP | ZDYVOID**  
*Building connected devices for the desk and the garden.*
