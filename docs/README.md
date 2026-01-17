# FarmPulse – Firmware Onboarding (ESP-IDF | LILYGO T3S3 v1.2)

> This project is not a hobby sketch. It is an industry‑grade firmware stack.
> Deterministic builds. Reproducible flashes. Zero guesswork.

---

## 1. Project Overview

**Target Hardware**  
- Board: **LILYGO‑T3S3 v1.2 LORA32**  
- MCU: **ESP32‑S3**  
- LoRa: **SX1276**  
- Display: **SSD1306 OLED**

**Framework**  
- **ESP‑IDF (official Espressif SDK)**

**External Dependencies (Git Submodules)**  
- LoRa Driver: `sx127x`  
  https://github.com/dernasherbrezon/sx127x
- OLED Driver: `esp-idf-ssd1306`  
  https://github.com/nopnop2002/esp-idf-ssd1306

These libraries are **locked as submodules** to guarantee build stability across the team.

---
