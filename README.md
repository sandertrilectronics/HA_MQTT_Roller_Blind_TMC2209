# HA MQTT Roller Blind TMC2209

This project is an ESP32-based smart roller blind controller using a TMC2209 stepper driver, with MQTT and Home Assistant integration. It features Wi-Fi connectivity, OTA updates, a web server, and persistent settings storage.

The hardware that is used for testing is the TTGO T-Motor board from LILYGO.

## Features
- Wi-Fi configuration (DHCP or static IP)
- MQTT integration for Home Assistant
- OTA firmware updates
- Web server for device management
- Stepper motor control via TMC2209 (via timer interrupt in ESP32)
- Persistent settings in NVS and SPIFFS
- Home Assistant cover, switch, button, and number entities

## Hardware Requirements
- ESP32 development board (e.g., LilyGO TTGO T-Motor ESP32 Motor Driver Module - TMC2209)
- TMC2209 stepper driver
- Stepper motor

## Software Requirements
- ESP-IDF (recommended v4.x or later)
- CMake
- Python (for ESP-IDF tools)

## Getting Started

### 1. Clone the Repository
```sh
git clone https://github.com/sandertrilectronics/HA_MQTT_Roller_Blind_TMC2209.git
cd HA_MQTT_Roller_Blind_TMC2209
```

### 2. Configure the Project
- Edit `main/secret.h` to set your Wi-Fi SSID, password, and MQTT credentials.
- Adjust `main/sys_cfg.c` or use the web server to set device-specific settings.

### 3. Build and Flash
```sh
idf.py build
idf.py -p <PORT> flash
```
Replace `<PORT>` with your ESP32 serial port.

### 4. Monitor Output
```sh
idf.py -p <PORT> monitor
```

### 5. Home Assistant Integration
- Add the device to Home Assistant via MQTT discovery.
- Entities for cover, switches, button, and max RPM will appear automatically.

## File Structure
- `main/` - Main application source code
- `components/` - Additional components (if any)
- `build/` - Build output (generated)
- `partitions.csv` - Partition table
- `sdkconfig` - ESP-IDF project configuration

## Customization
- Modify `main/ha_lib.c` and related files to adjust MQTT topics or Home Assistant behavior.
- Update `main/stp_drv.c` for stepper driver logic.

## License
See [LICENSE](LICENSE) for details.

## Credits
Developed by Sander for the Home Assistant community.