# Changelog

All notable changes to the FarmPulse Firmware will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [v0.1.0] - 2026-06-12
### Added
- Initial feature-complete firmware for makeshift zero-PCB hardware.
- LoRa mesh communication logic for gateway and node interaction.
- 3-phase voltage monitoring integration using ZMPT101B sensors.
- Relay control logic mapped to makeshift hardware GPIOs.
- Automated version embedding via ESP-IDF and Git tags.

### Known Issues
- Hardware mapping is strictly for the zero-PCB setup; pinouts will change in Rev 1 custom PCBs.
- OTA update functionality is currently bypassed/pending.