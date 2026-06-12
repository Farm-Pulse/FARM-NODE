# FarmPulse – Firmware Onboarding ( LILYGO T3S3 v1.2 | ESP-IDF)

> This project is not a hobby sketch. It is an industry‑grade firmware stack.
> Deterministic builds. Reproducible flashes. Zero guesswork.

---

## 1. Development Environment Setup (Windows)

This project assumes a **Windows + VS Code + ESP‑IDF** workflow.
No alternative setups are supported for team development.

---

## 1.1 Install Visual Studio Code

VS Code is the standard editor for this project.

**Download (Official):**  
https://code.visualstudio.com/

**Installation Notes**
- Use default installer options
- Ensure "Add to PATH" is enabled
- Restart the system after installation

---

## 1.2 Install ESP‑IDF Extension (VS Code)

ESP‑IDF is managed **entirely via the VS Code extension**.
Do not install ESP‑IDF manually.

### Steps
1. Open VS Code
2. Go to **Extensions** (`Ctrl + Shift + X`)
3. Search for **"Espressif IDF"**
4. Install the official extension by Espressif Systems

During first launch:
- Select **Express Install**
- Choose latest **stable ESP‑IDF** version (as recommended by project)
- Allow tools and Python environment to install

This process may take several minutes.

---

## 1.3 Verify ESP‑IDF Installation

1) Open VS Code Command Palette:

```text
Ctrl + Shift + P → ESP‑IDF: Open ESP‑IDF Terminal
```
2) Once ESP-IDF Terminal Opens: Type or copy below command to check the installed ESP-IDF version.

```text
idf.py --version
```

You must see:
- ESP‑IDF version displayed
- No error messages

If version is not shown, ESP‑IDF is not installed correctly.
Fix this before proceeding.

---

## 1.4 Build a Reference Blink Project (Mandatory)

Before touching FarmPulse code, verify the toolchain.

### Create Example Project

```text
Ctrl + Shift + P → ESP‑IDF: New Project
```

Select:
- Template: **blink**
- Target: **ESP32‑S3**
- Location: Any local workspace

### Build the Project

```text
Ctrl + Shift + P → ESP‑IDF: Build Project
```

Expected result:
- Build completes without errors

If build fails, do **not** continue.

### Reference Documentation

ESP‑IDF Getting Started (Official):  
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/

---

## 2. Project Overview

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

## 3. Cloning the Project (Correct Way)

This repository **uses Git submodules**. A normal clone is insufficient.

### 3.1 Fresh Clone

```bash
git clone --recurse-submodules <PROJECT_GIT_URL>
cd FarmPulse
```

### 3.2 If Already Cloned (Without Submodules)

```bash
git submodule update --init --recursive
```

**Verify:**
```bash
git submodule status
```
You must see commit hashes, not empty folders.

---

## 4. Project Structure (Mental Model)

```text
FarmPulse/
├── components/
│   ├── sx127x/              # LoRa driver (submodule)
│   └── ssd1306/             # OLED driver (submodule)
|   |__ lora/                # Custom driver
├── main/
│   ├── app_main.c
│   ├── lora_task.c
│   ├── display_task.c
│   └── CMakeLists.txt
├── sdkconfig
├── sdkconfig.defaults
├── CMakeLists.txt
└── docs/
    |__ Onboarding.md

```

**Rule:**  
- Application logic → `main/`  
- Reusable drivers → `components/`

Never mix these.

---

## 5. Configuration (`menuconfig`) – When and Why

### 5.1 Open Configuration Menu

```bash
idf.py menuconfig
```

### 5.2 When You MUST Reconfigure

Re-run `menuconfig` when:
- Board revision changes
- GPIO mapping changes
- LoRa frequency/region changes
- OLED I2C pins change
- ESP‑IDF version changes

If none of the above changed, **do not touch it**.

### 5.3 Critical Settings to Verify

#### ESP32‑S3 Settings
- `Component config → ESP32S3-specific`
- PSRAM enabled (if board supports it)

#### FreeRTOS
- Stack sizes (LoRa task is stack‑hungry)

#### SPI (LoRa)
- SPI host selected correctly
- GPIO mapping matches schematic

#### I2C (OLED)
- SDA / SCL pins
- Clock speed (100kHz recommended for stability)

---

## 6. Build the Firmware

From project root:

```bash
idf.py build
```

### Expected Output
- No warnings treated as errors
- `build/FarmPulse.bin` generated

If build fails:
- 90% chance ESP‑IDF environment not exported
- 10% chance submodules not initialized

---

## 7. Flashing the Board

### 7.1 Connect Hardware

- USB‑C cable (data cable, not charging‑only)
- Board in **BOOT mode** if required

### 7.2 Flash + Monitor

```bash
idf.py flash monitor
```

Default baud rate:
- `115200`

Exit monitor:
```text
Ctrl + ]
```

---

## 8. Common Developer Workflows

### Clean Rebuild (Use Sparingly)

```bash
idf.py fullclean
idf.py build
```
### If want to clean/fresh re-build (If major chnages)

- If some major changes done in **components/** or major drivers. Or after many error solving build process fails. And want to clean build, then we have to either run **reconfigure** command or delete the **build/** directory completely.

```bash
idf.py reconfigure      #To reconfigure the project.
    
              OR

Remove-Item -Recurse -Force build  #To delete build directory
```

### Update Submodules (When Team Updates Drivers)

```bash
git submodule update --remote --merge
```

Commit the updated submodule reference.

---

## 9. Team Rules (Non‑Negotiable)

- ❌ Do NOT edit code inside submodules directly
- ✅ Wrap or extend drivers in `main/`
- ❌ Do NOT commit `sdkconfig` blindly
- ✅ Discuss config changes before committing
- ❌ Do NOT push broken builds

Firmware is a shared nervous system. Treat it like one.

---

## 10. CI/CD Alignment (Minimal, Production‑Ready)

This repository is designed to integrate cleanly with CI pipelines.

**Recommended CI Checks**
- `idf.py build`
- Submodule integrity check
- Compiler warnings as errors

**What CI Must NOT Do**
- Flash hardware
- Modify `sdkconfig`

The goal is confidence, not cleverness.

---

## 11. First‑Day Developer Checklist

Every new developer must complete this on Day 1.

- [ ] ESP‑IDF installed and exported correctly
- [ ] Project cloned with `--recurse-submodules`
- [ ] `idf.py build` passes without errors
- [ ] Firmware flashed and serial logs visible
- [ ] Read `onboarding.md` fully

If any step fails, stop and fix it before writing code.

---

## 12. Firmware Development Rules (Professional Baseline)

### Code
- One feature per commit
- Clear commit messages (`type: short description`)
- No commented‑out dead code

### Configuration
- Treat `sdkconfig` as part of the API
- Changes require intent and review

### Hardware Awareness
- GPIOs are contracts
- Frequencies are regulatory
- Power paths are sacred

Assume every mistake ships.

---

## 13. Documentation Standard

Documentation is not optional.

Update docs when you:
- Add a task
- Change pin mapping
- Modify radio parameters
- Introduce a new dependency

If it’s not written, it doesn’t exist.

---

## 14. Final Note

This firmware is built to run unattended, in the field, under bad power and worse weather.

Simplicity is the real sophistication.

> Stable builds. Predictable behavior. Silent success.
