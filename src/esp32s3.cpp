// esp32s3.cpp - Bruce Firmware for ESP32-S3 Main Controller
// Kết nối với ESP32-C5 qua UART để điều khiển RF, NFC, IR

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

// ===================== Định nghĩa chân cho ESP32-S3 theo sơ đồ =====================
// Màn hình ST7789
#define TFT_MOSI    11
#define TFT_SCLK    12
#define TFT_CS      10
#define TFT_DC      14
#define TFT_RST     15
#define TFT_BL      21

// Thẻ nhớ MicroSD (dùng chung SPI với màn hình)
#define SD_CS       9
#define SD_MISO     13

// Phím 5-way (Joystick) - Sử dụng INPUT_PULLUP
#define BTN_UP      1
#define BTN_DOWN    2
#define BTN_LEFT    3
#define BTN_RIGHT   38
#define BTN_CENTER  39

// UART giao tiếp với ESP32-C5
#define UART_C5_TX  17
#define UART_C5_RX  18

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

#ifdef USE_HSPI_PORT
#ifndef VSPI
#define VSPI FSPI
#endif
SPIClass CC_NRF_SPI(VSPI);
#else
SPIClass CC_NRF_SPI(HSPI);
#endif

// Serial cho ESP32-C5
HardwareSerial SerialC5(1);

// Navigation Variables
volatile bool NextPress = false;
volatile bool PrevPress = false;
volatile bool UpPress = false;
volatile bool DownPress = false;
volatile bool SelPress = false;
volatile bool EscPress = false;
volatile bool AnyKeyPress = false;
volatile bool NextPagePress = false;
volatile bool PrevPagePress = false;
volatile bool LongPress = false;
volatile bool SerialCmdPress = false;
volatile int forceMenuOption = -1;
volatile uint8_t menuOptionType = 0;
String menuOptionLabel = "";
#ifdef HAS_ENCODER_LED
volatile int EncoderLedChange = 0;
#endif

TouchPoint touchPoint;
keyStroke KeyStroke;

TaskHandle_t xHandle;

void __attribute__((weak)) taskInputHandler(void *parameter) {
    auto timer = millis();
    while (true) {
        checkPowerSaveTime();
        if (!AnyKeyPress || millis() - timer > 75) {
            NextPress = false;
            PrevPress = false;
            UpPress = false;
            DownPress = false;
            SelPress = false;
            EscPress = false;
            AnyKeyPress = false;
            SerialCmdPress = false;
            NextPagePress = false;
            PrevPagePress = false;
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

// Public Globals Variables
unsigned long previousMillis = millis();
int prog_handler = 0;
String cachedPassword = "";
int8_t interpreter_state = -1;
bool sdcardMounted = false;
bool gpsConnected = false;
bool c5Connected = false;

// wifi globals
bool wifiConnected = false;
bool isWebUIActive = false;
String wifiIP;

bool BLEConnected = false;
bool returnToMenu;
bool isSleeping = false;
bool isScreenOff = false;
bool dimmer = false;
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

// ===================== Hàm giao tiếp với C5 =====================
void initC5Communication() {
    SerialC5.begin(115200, SERIAL_8N1, UART_C5_RX, UART_C5_TX);
    delay(100);
    
    // Gửi lệnh kiểm tra kết nối
    SerialC5.println("PING");
    
    unsigned long startTime = millis();
    while (millis() - startTime < 2000) {
        if (SerialC5.available()) {
            String response = SerialC5.readStringUntil('\n');
            response.trim();
            if (response == "PONG") {
                c5Connected = true;
                Serial.println("[C5] Connected successfully!");
                break;
            }
        }
        delay(10);
    }
    
    if (!c5Connected) {
        Serial.println("[C5] Not responding!");
    }
}

void sendToC5(String cmd) {
    if (!c5Connected) {
        Serial.println("[C5] Not connected!");
        return;
    }
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

// ===================== Hàm xử lý phím 5-way =====================
void initJoystick() {
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_CENTER, INPUT_PULLUP);
}

void readJoystick() {
    // Cập nhật biến điều hướng dựa trên trạng thái phím
    // INPUT_PULLUP nên mức LOW là nhấn
    
    if (digitalRead(BTN_UP) == LOW) {
        UpPress = true;
        AnyKeyPress = true;
    }
    
    if (digitalRead(BTN_DOWN) == LOW) {
        DownPress = true;
        AnyKeyPress = true;
    }
    
    if (digitalRead(BTN_LEFT) == LOW) {
        PrevPress = true;
        AnyKeyPress = true;
    }
    
    if (digitalRead(BTN_RIGHT) == LOW) {
        NextPress = true;
        AnyKeyPress = true;
    }
    
    if (digitalRead(BTN_CENTER) == LOW) {
        SelPress = true;
        AnyKeyPress = true;
    }
}

void InputHandler() {
    readJoystick();
}

// ===================== Khởi tạo SPI =====================
void initSPI() {
    // Khởi tạo SPI bus cho màn hình và SD Card
    sdcardSPI.begin(TFT_SCLK, SD_MISO, TFT_MOSI, SD_CS);
}

/*********************************************************************
 **  Function: begin_storage
 **  Config LittleFS and SD storage
 *********************************************************************/
void begin_storage() {
    if (!LittleFS.begin(true)) { 
        LittleFS.format(); 
        LittleFS.begin(); 
    }
    bool checkFS = setupSdCard();
    bruceConfig.fromFile(checkFS);
    bruceConfigPins.fromFile(checkFS);
}

/*********************************************************************
 **  Function: _setup_gpio()
 **  Setup GPIO theo sơ đồ
 *********************************************************************/
void _setup_gpio() {
    initSPI();
    initJoystick();
    initC5Communication();
}

void _post_setup_gpio() __attribute__((weak));
void _post_setup_gpio() {}

/*********************************************************************
 **  Function: setup_gpio
 **  Setup GPIO pins
 *********************************************************************/
void setup_gpio() {
    _setup_gpio();
    ioExpander.init(IO_EXPANDER_ADDRESS, &Wire);
    
    // CC1101 được điều khiển bởi C5, không cần khởi tạo ở đây
}

/*********************************************************************
 **  Function: begin_tft
 **  Config tft
 *********************************************************************/
void begin_tft() {
    // Cấu hình SPI cho màn hình
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

/*********************************************************************
 **  Function: boot_screen
 **  Draw boot screen
 *********************************************************************/
void boot_screen() {
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    tft.drawPixel(0, 0, bruceConfig.bgColor);
    tft.drawCentreString("Bruce", tftWidth / 2, 10, 1);
    tft.setTextSize(FP);
    tft.drawCentreString(BRUCE_VERSION, tftWidth / 2, 25, 1);
    tft.setTextSize(FM);
    tft.drawCentreString(
        "PREDATORY FIRMWARE", tftWidth / 2, tftHeight + 2, 1
    );
}

/*********************************************************************
 **  Function: boot_screen_anim
 **  Draw boot screen animation
 *********************************************************************/
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
                    drawImg(
                        *bruceConfig.themeFS(),
                        bruceConfig.getThemeItemImg(bruceConfig.theme.paths.boot_img),
                        0, 0, true, 3600
                    );
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

/*********************************************************************
 **  Function: init_clock
 **  Clock initialisation
 *********************************************************************/
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

/*********************************************************************
 **  Function: init_led
 **  LED initialisation
 *********************************************************************/
void init_led() {
#ifdef HAS_RGB_LED
    beginLed();
#endif
    // Điều khiển đèn nền TFT
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
}

/*********************************************************************
 **  Function: startup_sound
 **  Play startup sound
 *********************************************************************/
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

// ===================== Các hàm điều khiển RF qua C5 =====================
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

void nfcRead() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("Place NFC tag", tftWidth / 2, tftHeight / 3, 2);
    sendToC5("NFC_READ");
    
    unsigned long start = millis();
    while (millis() - start < 10000) {
        if (SerialC5.available()) {
            String resp = SerialC5.readStringUntil('\n');
            if (resp.startsWith("UID:")) {
                tft.drawCentreString(resp, tftWidth / 2, tftHeight / 2, 2);
                break;
            } else if (resp == "NO_TAG") {
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

void tvBgone() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawCentreString("TV-B-Gone", tftWidth / 2, tftHeight / 3, 2);
    tft.drawCentreString("Sending IR codes...", tftWidth / 2, tftHeight / 2, 1);
    sendToC5("IR_TVBGONE");
    
    unsigned long start = millis();
    while (millis() - start < 15000) {
        if (SerialC5.available()) {
            String resp = SerialC5.readStringUntil('\n');
            if (resp == "TVBGONE_DONE") break;
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

// ===================== Các hàm WiFi =====================
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
    dnsServer.start(53, "*", WiFi.softAPIP());
    server.on("/", []() { server.send(200, "text/html", "<h1>Login</h1>"); });
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

// ===================== Menu và điều hướng =====================
// Các menu sẽ được quản lý bởi Bruce framework
// Các hàm trên sẽ được gọi từ main menu

/*********************************************************************
 **  Function: setup
 **  Main setup function
 *********************************************************************/
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

    // Cấu hình WiFi
    const wifi_country_t country = {
        .cc = "US",
        .schan = 1,
        .nchan = 14,
#ifdef CONFIG_ESP_PHY_MAX_TX_POWER
        .max_tx_power = CONFIG_ESP_PHY_MAX_TX_POWER,
#endif
        .policy = WIFI_COUNTRY_POLICY_MANUAL
    };

    esp_wifi_set_max_tx_power(80);
    esp_wifi_set_country(&country);

    _post_setup_gpio();

    xTaskCreate(
        taskInputHandler,
        "InputHandler",
        INPUT_HANDLER_TASK_STACK_SIZE,
        NULL,
        2,
        &xHandle
    );
    
#if defined(HAS_SCREEN)
    bruceConfig.openThemeFile(bruceConfig.themeFS(), bruceConfig.themePath, false);
    if (!bruceConfig.instantBoot) {
        boot_screen_anim();
        startup_sound();
    }
    if (bruceConfig.wifiAtStartup) {
        log_i("Loading Wifi at Startup");
        xTaskCreate(
            wifiConnectTask,
            "wifiConnectTask",
            4096,
            NULL,
            2,
            NULL
        );
    }
#endif
    
    startSerialCommandsHandlerTask(true);
    wakeUpScreen();
    
    if (bruceConfig.startupApp != "" && !startupApp.startApp(bruceConfig.startupApp)) {
        bruceConfig.setStartupApp("");
    }
}

/**********************************************************************
 **  Function: loop
 **  Main loop
 **********************************************************************/
#if defined(HAS_SCREEN)
void loop() {
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
    if (interpreter_state > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        interpreter_state = 2;
        Serial.println("Entering interpreter...");
        while (interpreter_state > 0) { 
            vTaskDelay(pdMS_TO_TICKS(500)); 
        }
        if (interpreter_state == 0) {
            Serial.println("Interpreter put to background.");
        } else {
            Serial.println("Exiting interpreter...");
        }
        if (interpreter_state == -1) { 
            interpreterTaskHandler = NULL; 
        }
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
    Serial.println(
        "\n"
        "██████  ██████  ██    ██  ██████ ███████ \n"
        "██   ██ ██   ██ ██    ██ ██      ██      \n"
        "██████  ██████  ██    ██ ██      █████   \n"
        "██   ██ ██   ██ ██    ██ ██      ██      \n"
        "██████  ██   ██  ██████   ██████ ███████ \n"
        "                                         \n"
        "         PREDATORY FIRMWARE\n\n"
        "Tips: Connect to the WebUI for better experience\n"
        "      Add your network by sending: wifi add ssid password\n\n"
        "At your command:"
    );

    tft.fillScreen(bruceConfig.bgColor);
    mainMenu.begin();
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
#endif
