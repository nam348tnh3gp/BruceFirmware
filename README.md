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
```

ESP32-C5

```bash
pio run -e esp32-c5
pio run -e esp32-c5 -t upload
```

📂 Thư mục SD

Copy toàn bộ nội dung thư mục sd_files/ vào thẻ nhớ FAT32 trước khi sử dụng.

🎮 Điều khiển

· Phím UP/DOWN: Di chuyển trong menu
· Phím CENTER: Chọn chức năng
· Phím LEFT: Quay lại menu chính

⚠️ Disclaimer

Chỉ sử dụng cho mục đích học tập và thử nghiệm trong môi trường được ủy quyền.
