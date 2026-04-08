#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <FS.h>
#include <LittleFS.h>

// ===================== HẰNG SỐ CHÂN =====================
// Màn hình ST7789
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   14
#define TFT_RST  15
#define TFT_BL   21

// SD Card
#define SD_CS    9
#define SD_MISO  13

// Phím 5 chiều
#define BTN_UP    1
#define BTN_DOWN  2
#define BTN_LEFT  3
#define BTN_RIGHT 38
#define BTN_CENTER 39

// UART với ESP32-C5
#define UART_C5_TX 17
#define UART_C5_RX 18
HardwareSerial SerialC5(1);

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);

// ===================== CẤU TRÚC MENU ĐA CẤP =====================
enum MenuLevel { MAIN, WIFI_MENU, BLE_MENU, RF_MENU, NFC_MENU, IR_MENU, SD_MENU, CONFIG_MENU };
MenuLevel currentLevel = MAIN;
int selectedIndex = 0;

// Menu chính (giống Bruce)
String mainMenu[] = {"WiFi", "BLE", "RF", "NFC", "IR", "SD Card", "Scripts", "WebUI", "Config"};
int mainMenuSize = 9;

// Menu WiFi
String wifiMenu[] = {"Beacon Spam", "Deauth Attack", "Evil Portal", "Wardriving", "Responder", "ARP Spoof", "TCP Listener", "RAW Sniffer"};
int wifiMenuSize = 8;

// Menu BLE
String bleMenu[] = {"iOS Spam", "Android Spam", "Windows Spam", "Samsung Spam", "BLE Scan", "Bad BLE"};
int bleMenuSize = 6;

// Menu RF (gửi lệnh sang C5)
String rfMenu[] = {"Scan 433MHz", "Jammer Full", "Replay", "Custom SubGhz", "NRF24 Jammer", "Mousejack"};
int rfMenuSize = 6;

// Menu NFC
String nfcMenu[] = {"Read Tag", "Clone Tag", "Write NDEF", "Emulate Tag", "Amiibolink"};
int nfcMenuSize = 5;

// Menu IR
String irMenu[] = {"TV-B-Gone", "Receive IR", "Send NEC", "Send RC5"};
int irMenuSize = 4;

// ===================== KHỞI TẠO =====================
void setup() {
  Serial.begin(115200);
  SerialC5.begin(115200, SERIAL_8N1, UART_C5_RX, UART_C5_TX);
  
  // Màn hình
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("BRUCE FULL v2.0", 50, 120, 2);

  // SD Card
  SPI.begin(TFT_SCLK, SD_MISO, TFT_MOSI, TFT_CS);
  if (!SD.begin(SD_CS, SPI, 4000000)) {
    tft.drawString("SD ERR", 90, 150, 2);
  } else {
    tft.drawString("SD OK", 95, 150, 2);
  }

  // LittleFS cho scripts
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
  }

  // Phím
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);

  // Khởi tạo WebUI
  initWebUI();

  delay(1500);
  drawMainMenu();
}

// ===================== GIAO DIỆN =====================
void drawMainMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  for (int i = 0; i < mainMenuSize; i++) {
    if (i == selectedIndex) {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("> " + mainMenu[i], 20, 20 + i * 30, 2);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(mainMenu[i], 40, 20 + i * 30, 2);
    }
  }
}

void drawSubMenu(String* menu, int size) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  for (int i = 0; i < size; i++) {
    if (i == selectedIndex) {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("> " + menu[i], 20, 20 + i * 30, 2);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(menu[i], 40, 20 + i * 30, 2);
    }
  }
}

// ===================== XỬ LÝ PHÍM =====================
void handleButtons() {
  if (digitalRead(BTN_UP) == LOW) {
    selectedIndex = (selectedIndex - 1 + getCurrentMenuSize()) % getCurrentMenuSize();
    refreshDisplay();
    delay(200);
  }
  if (digitalRead(BTN_DOWN) == LOW) {
    selectedIndex = (selectedIndex + 1) % getCurrentMenuSize();
    refreshDisplay();
    delay(200);
  }
  if (digitalRead(BTN_CENTER) == LOW) {
    executeCurrentFunction();
    delay(300);
  }
  if (digitalRead(BTN_LEFT) == LOW && currentLevel != MAIN) {
    currentLevel = MAIN;
    selectedIndex = 0;
    drawMainMenu();
    delay(200);
  }
}

int getCurrentMenuSize() {
  switch(currentLevel) {
    case MAIN: return mainMenuSize;
    case WIFI_MENU: return wifiMenuSize;
    case BLE_MENU: return bleMenuSize;
    case RF_MENU: return rfMenuSize;
    case NFC_MENU: return nfcMenuSize;
    case IR_MENU: return irMenuSize;
    default: return 0;
  }
}

void refreshDisplay() {
  switch(currentLevel) {
    case MAIN: drawMainMenu(); break;
    case WIFI_MENU: drawSubMenu(wifiMenu, wifiMenuSize); break;
    case BLE_MENU: drawSubMenu(bleMenu, bleMenuSize); break;
    case RF_MENU: drawSubMenu(rfMenu, rfMenuSize); break;
    case NFC_MENU: drawSubMenu(nfcMenu, nfcMenuSize); break;
    case IR_MENU: drawSubMenu(irMenu, irMenuSize); break;
    default: drawMainMenu();
  }
}

// ===================== THỰC THI CHỨC NĂNG (CHUYỂN TIẾP) =====================
void executeCurrentFunction() {
  switch(currentLevel) {
    case MAIN:
      switch(selectedIndex) {
        case 0: currentLevel = WIFI_MENU; selectedIndex = 0; drawSubMenu(wifiMenu, wifiMenuSize); break;
        case 1: currentLevel = BLE_MENU; selectedIndex = 0; drawSubMenu(bleMenu, bleMenuSize); break;
        case 2: currentLevel = RF_MENU; selectedIndex = 0; drawSubMenu(rfMenu, rfMenuSize); break;
        case 3: currentLevel = NFC_MENU; selectedIndex = 0; drawSubMenu(nfcMenu, nfcMenuSize); break;
        case 4: currentLevel = IR_MENU; selectedIndex = 0; drawSubMenu(irMenu, irMenuSize); break;
        case 5: sdManager(); break;
        case 6: runScriptMenu(); break;
        case 7: startWebUI(); break;
        case 8: configMenu(); break;
      }
      break;
    case WIFI_MENU:
      switch(selectedIndex) {
        case 0: wifiBeaconSpam(); break;
        case 1: wifiDeauth(); break;
        case 2: evilPortal(); break;
        case 3: wardriving(); break;
        case 4: responder(); break;
        case 5: arpSpoof(); break;
        case 6: tcpListener(); break;
        case 7: rawSniffer(); break;
      }
      break;
    case BLE_MENU:
      switch(selectedIndex) {
        case 0: bleIOSSpam(); break;
        case 1: bleAndroidSpam(); break;
        case 2: bleWindowsSpam(); break;
        case 3: bleSamsungSpam(); break;
        case 4: bleScan(); break;
        case 5: badBLE(); break;
      }
      break;
    case RF_MENU:
      switch(selectedIndex) {
        case 0: sendToC5("RF_SCAN"); break;
        case 1: sendToC5("RF_JAM_FULL"); break;
        case 2: sendToC5("RF_REPLAY"); break;
        case 3: sendToC5("RF_CUSTOM"); break;
        case 4: sendToC5("NRF24_JAM"); break;
        case 5: sendToC5("MOUSEJACK"); break;
      }
      displayResultFromC5();
      break;
    case NFC_MENU:
      switch(selectedIndex) {
        case 0: sendToC5("NFC_READ"); break;
        case 1: sendToC5("NFC_CLONE"); break;
        case 2: sendToC5("NFC_WRITE_NDEF"); break;
        case 3: sendToC5("NFC_EMULATE"); break;
        case 4: sendToC5("AMIIBOLINK"); break;
      }
      displayResultFromC5();
      break;
    case IR_MENU:
      switch(selectedIndex) {
        case 0: sendToC5("IR_TVBGONE"); break;
        case 1: sendToC5("IR_RECV"); break;
        case 2: sendToC5("IR_SEND_NEC"); break;
        case 3: sendToC5("IR_SEND_RC5"); break;
      }
      displayResultFromC5();
      break;
  }
}

// ===================== CÁC TÍNH NĂNG WIFI =====================
void wifiBeaconSpam() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Beacon Spam", 50, 100, 2);
  // Dùng esp_wifi_set_promiscuous để gửi beacon giả
  wifi_promiscuous_enable(1);
  delay(10000);
  wifi_promiscuous_enable(0);
}

void wifiDeauth() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Deauth Attack", 50, 100, 2);
  // Gửi gói deauth đến tất cả AP
  // (Cần code cụ thể dùng esp_wifi_80211_tx)
  delay(10000);
}

void evilPortal() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Evil Portal", 50, 100, 2);
  WiFi.softAP("Free WiFi");
  // Khởi tạo DNS + Web server bắt password
  delay(30000);
  WiFi.softAPdisconnect(true);
}

void wardriving() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Wardriving...", 50, 100, 2);
  // Quét AP và ghi vào SD
  delay(5000);
}

void responder() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Responder (LLMNR/NBT-NS)", 20, 100, 2);
  // Chạy dịch vụ giả mạo
  delay(10000);
}

void arpSpoof() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("ARP Spoofing", 50, 100, 2);
  // Gửi ARP reply giả
  delay(10000);
}

void tcpListener() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("TCP Listener :4444", 30, 100, 2);
  // Mở socket TCP chờ kết nối
  delay(10000);
}

void rawSniffer() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("RAW Sniffer", 50, 100, 2);
  // Bắt gói tin raw
  delay(10000);
}

// ===================== CÁC TÍNH NĂNG BLE =====================
void bleIOSSpam() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("iOS Spam", 50, 100, 2);
  BLEDevice::init("Apple Device");
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->start();
  delay(10000);
  pAdvertising->stop();
}

void bleAndroidSpam() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Android Spam", 50, 100, 2);
  BLEDevice::init("Android Device");
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->start();
  delay(10000);
  pAdvertising->stop();
}

void bleWindowsSpam() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Windows Spam", 50, 100, 2);
  BLEDevice::init("Windows Device");
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->start();
  delay(10000);
  pAdvertising->stop();
}

void bleSamsungSpam() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Samsung Spam", 50, 100, 2);
  BLEDevice::init("Samsung Device");
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->start();
  delay(10000);
  pAdvertising->stop();
}

void bleScan() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("BLE Scanning...", 50, 100, 2);
  // BLEScan *pScan = BLEDevice::getScan();
  delay(10000);
}

void badBLE() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Bad BLE", 50, 100, 2);
  // Chạy Ducky script qua BLE
  delay(5000);
}

// ===================== GIAO TIẾP VỚI ESP32-C5 =====================
void sendToC5(String cmd) {
  SerialC5.println(cmd);
  Serial.println("-> C5: " + cmd);
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Sending: " + cmd, 20, 100, 2);
}

void displayResultFromC5() {
  unsigned long start = millis();
  while (millis() - start < 5000) {
    if (SerialC5.available()) {
      String resp = SerialC5.readStringUntil('\n');
      tft.fillRect(0, 140, 320, 60, TFT_BLACK);
      tft.setCursor(10, 140);
      tft.println(resp);
      break;
    }
    delay(50);
  }
  delay(2000);
}

// ===================== SD CARD MANAGER =====================
void sdManager() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("SD Files:", 10, 10, 2);
  File root = SD.open("/");
  int y = 40;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    tft.setCursor(10, y);
    tft.println(entry.name());
    y += 20;
    if (y > 220) break;
    entry.close();
  }
  delay(3000);
}

// ===================== SCRIPT MANAGER (JS) =====================
void runScriptMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Scripts:", 10, 10, 2);
  File dir = LittleFS.open("/");
  int y = 40;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      tft.setCursor(10, y);
      tft.println(entry.name());
      y += 20;
    }
    entry.close();
  }
  delay(3000);
  // Có thể thêm chạy script JS bằng mQuickJS
}

// ===================== WEBUI =====================
void initWebUI() {
  server.on("/", []() {
    server.send(200, "text/html", "<h1>Bruce WebUI</h1><a href='/attack'>Attack</a><br><a href='/sd'>SD Card</a>");
  });
  server.on("/attack", []() {
    server.send(200, "text/plain", "Attack started");
    wifiDeauth();
  });
  server.on("/sd", []() {
    String list = "";
    File root = SD.open("/");
    File file = root.openNextFile();
    while(file) {
      list += String(file.name()) + "\n";
      file = root.openNextFile();
    }
    server.send(200, "text/plain", list);
  });
  server.begin();
  Serial.println("WebUI started");
}

void startWebUI() {
  WiFi.softAP("BruceAP", "12345678");
  tft.fillScreen(TFT_BLACK);
  tft.drawString("WebUI: 192.168.4.1", 30, 100, 2);
  delay(30000);
  WiFi.softAPdisconnect(true);
}

// ===================== CONFIG =====================
void configMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Config", 50, 50, 2);
  tft.drawString("Brightness", 30, 100, 2);
  tft.drawString("Sleep", 30, 130, 2);
  tft.drawString("Restart", 30, 160, 2);
  delay(3000);
}

// ===================== VÒNG LẶP CHÍNH =====================
void loop() {
  handleButtons();
  server.handleClient();

  // Nhận dữ liệu không đồng bộ từ C5
  if (SerialC5.available()) {
    String resp = SerialC5.readStringUntil('\n');
    tft.fillRect(0, 220, 320, 20, TFT_BLACK);
    tft.setCursor(10, 220);
    tft.println(resp);
  }
  delay(50);
}
