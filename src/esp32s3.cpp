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

io_expander ioExpander;
BruceConfig bruceConfig;
BruceConfigPins bruceConfigPins;

SerialCli serialCli;
USBSerial USBserial;
SerialDevice *serialDevice = &USBserial;

StartupApp startupApp;
String startupAppJSInterpreterFile = "";

MainMenu mainMenu;
SPIClass sdcardSPI;
#ifdef USE_HSPI_PORT
#ifndef VSPI
#define VSPI FSPI
#endif
SPIClass CC_NRF_SPI(VSPI);
#else
SPIClass CC_NRF_SPI(HSPI);
#endif

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

// Protected global variables
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

// ========== GIAO TIẾP VỚI SLAVE (ESP32-C5) ==========
#define SLAVE_SERIAL Serial2
#define SLAVE_TX_PIN 17
#define SLAVE_RX_PIN 18

void sendToSlave(String command) {
    SLAVE_SERIAL.println(command);
    Serial.println("[Master] Sent to Slave: " + command);
}

String readFromSlave() {
    if (SLAVE_SERIAL.available()) {
        return SLAVE_SERIAL.readStringUntil('\n');
    }
    return "";
}

void setupSlaveCommunication() {
    SLAVE_SERIAL.begin(115200, SERIAL_8N1, SLAVE_RX_PIN, SLAVE_TX_PIN);
    Serial.println("[Master] UART with Slave initialized on pins TX=17, RX=18");
}

/*********************************************************************
 **  Function: begin_storage
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

void _setup_gpio() __attribute__((weak));
void _setup_gpio() {}

void _post_setup_gpio() __attribute__((weak));
void _post_setup_gpio() {}

/*********************************************************************
 **  Function: setup_gpio
 *********************************************************************/
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

/*********************************************************************
 **  Function: begin_tft
 *********************************************************************/
void begin_tft() {
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
 *********************************************************************/
void boot_screen() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    tft.drawCentreString("Bruce", tftWidth / 2, 10, 1);
    tft.setTextSize(FP);
    tft.drawCentreString(BRUCE_VERSION, tftWidth / 2, 30, 1);
    tft.setTextSize(FM);
    tft.drawCentreString("PREDATORY FIRMWARE", tftWidth / 2, tftHeight - 10, 1);
}

/*********************************************************************
 **  Function: boot_screen_anim
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
    
    while (millis() < i + 5000) {
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
    }
    tft.fillScreen(bruceConfig.bgColor);
}

/*********************************************************************
 **  Function: init_clock
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
    timeinfo.tm_mon = 5;
    timeinfo.tm_mday = 20;
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

/*********************************************************************
 **  Function: setup
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
    
    // Khởi tạo giao tiếp với Slave
    setupSlaveCommunication();
    
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
    
    // Gửi tín hiệu khởi tạo cho Slave
    sendToSlave("INIT_SLAVE");
    
    if (bruceConfig.startupApp != "" && !startupApp.startApp(bruceConfig.startupApp)) {
        bruceConfig.setStartupApp("");
    }
}

/**********************************************************************
 **  Function: loop
 **********************************************************************/
#if defined(HAS_SCREEN)
void loop() {
    // Đọc phản hồi từ Slave
    String slaveResponse = readFromSlave();
    if (slaveResponse.length() > 0) {
        Serial.println("[Master] From Slave: " + slaveResponse);
        // Xử lý response từ slave nếu cần
    }
    
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
    Serial.println("\n"
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
