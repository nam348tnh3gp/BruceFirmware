# BruceFirmware cho ESP32-S3 + ESP32-C5

Firmware tùy chỉnh từ Bruce gốc, chạy trên 2 vi điều khiển:
- **ESP32-S3**: Giao diện ST7789, WiFi/BLE tấn công, SD card, WebUI
- **ESP32-C5**: RF (CC1101, NRF24), NFC, IR

## 📌 Sơ đồ chân
Xem chi tiết tại `include/pins_config.h`

## 🛠 Cách build

### ESP32-S3
```bash
pio run -e esp32-s3
pio run -e esp32-s3 -t upload
