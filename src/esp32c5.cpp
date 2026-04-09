// esp32c5.cpp - ESP32-C5 RF & Peripheral Controller (FULL VERSION - FIXED)
// Kết nối với ESP32-S3 qua UART

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <IRremote.h>

// ===================== KHAI BÁO HÀM =====================
void rfScan();
void rfJamFull();
void rfReplay();
void rfCustom();
void rfCapture();
void nrfScan();
void nrfJam();
void mousejack();
void nfcRead();
void nfcClone();
void nfcWriteNDEF();
void nfcEmulate();
void amiibolink();
void tvBgone();
void irReceive();
void irSendNEC();
void irSendRC5();
void irSendRaw();
void processCommand(String cmd);
void sendToS3(String msg);
void blinkLED(int times, int duration);

// ===================== CHÂN KẾT NỐI (ĐÃ FIX) =====================
#define UART_S3_RX    17
#define UART_S3_TX    18
#define UART_BAUD     115200

// SPI - NRF24 và CC1101 dùng chung bus SPI
#define SPI_MOSI      25
#define SPI_MISO      26
#define SPI_SCK       27
#define NRF_CSN       14
#define NRF_CE        13
#define CC1101_CSN    15
#define CC1101_GDO0   28

// SD Card - FIX: Thêm chân CS riêng cho SD
#define SD_CS         21      // <--- FIX: Chân CS riêng cho SD Card (không xung đột với CC1101)

// I2C - NFC PN532
#define I2C_SDA       4
#define I2C_SCL       5

// Hồng ngoại
#define IR_RX         23
#define IR_TX         24

// LED trạng thái
#define STATUS_LED    2

// ===================== KHỞI TẠO ĐỐI TƯỢNG =====================
HardwareSerial SerialS3(1);
RF24 radio(NRF_CE, NRF_CSN);
TwoWire I2C_NFC = TwoWire(0);
PN532_I2C nfcInterface(I2C_NFC);
PN532 nfcShield(nfcInterface);

// ===================== BIẾN TOÀN CỤC =====================
volatile bool rfJamActive = false;
volatile bool nrfJamActive = false;
volatile bool rfScanning = false;
volatile bool nrfScanning = false;
bool cc1101Initialized = false;
bool nrf24Initialized = false;
bool nfcInitialized = false;
String commandBuffer = "";
unsigned long lastJamTime = 0;
unsigned long lastScanTime = 0;

// Dải tần quét RF
float rfFrequencies[] = {315.0, 433.92, 868.0, 915.0, 2400.0};
int numRfFreqs = 5;

// Kênh NRF24
uint8_t nrfChannels[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
    46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75,
    76, 77, 78, 79, 80, 81, 82, 83
};
int numNrfChannels = sizeof(nrfChannels) / sizeof(nrfChannels[0]);

// ===================== GỬI DỮ LIỆU VỀ S3 =====================
void sendToS3(String msg) {
    SerialS3.println(msg);
    Serial.print("[S3] <- ");
    Serial.println(msg);
}

// ===================== LED BÁO =====================
void blinkLED(int times, int duration) {
    pinMode(STATUS_LED, OUTPUT);
    for (int i = 0; i < times; i++) {
        digitalWrite(STATUS_LED, HIGH);
        delay(duration);
        digitalWrite(STATUS_LED, LOW);
        if (i < times - 1) delay(duration);
    }
}

// ===================== KHỞI TẠO CC1101 =====================
bool initCC1101() {
    Serial.println("[CC1101] Initializing...");
    
    // FIX: Cấu hình SPI pins TRƯỚC khi gọi Init()
    ELECHOUSE_cc1101.setSpiPin(SPI_SCK, SPI_MISO, SPI_MOSI, CC1101_CSN);
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO0); // FIX: Thêm GDO pin
    
    int ret = ELECHOUSE_cc1101.Init();
    if (ret != 0) {
        Serial.printf("[CC1101] Init failed with code: %d\n", ret);
        return false;
    }
    
    delay(100);
    
    byte version = ELECHOUSE_cc1101.getVersion();
    if (version == 0 || version == 0xFF) {
        Serial.println("[CC1101] NOT FOUND! Check wiring.");
        return false;
    }
    
    ELECHOUSE_cc1101.setCCMode(1);
    ELECHOUSE_cc1101.setModulation(0);
    ELECHOUSE_cc1101.setMHZ(433.92);
    ELECHOUSE_cc1101.setPower(12);
    ELECHOUSE_cc1101.setPA(10);
    ELECHOUSE_cc1101.SetRx();
    
    Serial.printf("[CC1101] OK - Version: 0x%02X\n", version);
    return true;
}

// ===================== KHỞI TẠO NRF24 =====================
bool initNRF24() {
    Serial.println("[NRF24] Initializing...");
    
    if (!radio.begin()) {
        Serial.println("[NRF24] NOT FOUND! Check wiring.");
        return false;
    }
    
    radio.setChannel(100);
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_250KBPS);
    radio.setPayloadSize(32);
    radio.setAutoAck(true);
    radio.setRetries(5, 15);
    radio.openWritingPipe(0xE7E7E7E7E7LL);
    radio.openReadingPipe(1, 0xE7E7E7E7E7LL);
    radio.startListening();
    
    Serial.println("[NRF24] OK");
    return true;
}

// ===================== KHỞI TẠO NFC =====================
bool initNFC() {
    Serial.println("[NFC] Initializing PN532...");
    
    I2C_NFC.begin(I2C_SDA, I2C_SCL, 100000);
    nfcShield.begin();
    
    uint32_t version = nfcShield.getFirmwareVersion();
    if (!version) {
        Serial.println("[NFC] PN532 NOT FOUND! Check I2C wiring.");
        return false;
    }
    
    Serial.printf("[NFC] OK - Chip PN5%02x, FW %d.%d\n",
                  (version >> 24) & 0xFF,
                  (version >> 16) & 0xFF,
                  (version >> 8) & 0xFF);
    
    nfcShield.SAMConfig();
    return true;
}

// ===================== KHỞI TẠO IR =====================
void initIR() {
    Serial.println("[IR] Initializing...");
    IrReceiver.begin(IR_RX, false);
    IrSender.begin(IR_TX, false);
    Serial.println("[IR] OK");
}

// ===================== RF SCAN =====================
void rfScan() {
    if (!cc1101Initialized) {
        sendToS3("RF_SCAN_ERR:CC1101 not initialized");
        return;
    }
    
    if (rfScanning) {
        sendToS3("RF_SCAN_ALREADY_RUNNING");
        return;
    }
    
    rfScanning = true;
    sendToS3("RF_SCAN_START");
    
    for (int i = 0; i < numRfFreqs && rfScanning; i++) {
        float freq = rfFrequencies[i];
        ELECHOUSE_cc1101.setMHZ(freq);
        ELECHOUSE_cc1101.SetRx();
        delay(100);
        
        int signalCount = 0;
        unsigned long start = millis();
        
        while (millis() - start < 500 && rfScanning) {
            if (ELECHOUSE_cc1101.CheckReceiveFlag()) {
                uint8_t buffer[64];
                uint8_t len = ELECHOUSE_cc1101.ReceiveData(buffer);
                if (len > 0) {
                    signalCount++;
                    char buf[80];
                    sprintf(buf, "RF_SIGNAL:%.2fMHz,len=%d", freq, len);
                    sendToS3(buf);
                    
                    String hexData = "RF_DATA:";
                    for (int j = 0; j < len && j < 32; j++) {
                        if (buffer[j] < 0x10) hexData += "0";
                        hexData += String(buffer[j], HEX);
                    }
                    sendToS3(hexData);
                    blinkLED(1, 30);
                }
            }
            vTaskDelay(1);
        }
        
        char buf[64];
        sprintf(buf, "RF_SCAN_RESULT:%.2fMHz,%d", freq, signalCount);
        sendToS3(buf);
        
        // FIX: Check for stop command during scan
        if (SerialS3.available()) {
            String cmd = SerialS3.readStringUntil('\n');
            if (cmd == "RF_SCAN_STOP") break;
        }
    }
    
    rfScanning = false;
    ELECHOUSE_cc1101.SetRx();
    sendToS3("RF_SCAN_DONE");
}

// ===================== RF JAMMER =====================
void rfJamFull() {
    if (!cc1101Initialized) {
        sendToS3("RF_JAM_ERR:CC1101 not initialized");
        return;
    }
    
    rfJamActive = !rfJamActive;
    
    if (rfJamActive) {
        ELECHOUSE_cc1101.SetTx();
        sendToS3("RF_JAM_ON");
        Serial.println("[RF] Jammer ACTIVATED");
    } else {
        ELECHOUSE_cc1101.SetRx();
        sendToS3("RF_JAM_OFF");
        Serial.println("[RF] Jammer DEACTIVATED");
    }
}

// ===================== RF REPLAY =====================
void rfReplay() {
    if (!cc1101Initialized) {
        sendToS3("RF_REPLAY_ERR:CC1101 not initialized");
        return;
    }
    
    sendToS3("RF_REPLAY_START");
    
    // FIX: Sử dụng SD_CS đã định nghĩa
    if (!SD.exists("/rf_capture.bin")) {
        sendToS3("RF_REPLAY_NO_FILE");
        return;
    }
    
    File file = SD.open("/rf_capture.bin", FILE_READ);
    if (!file) {
        sendToS3("RF_REPLAY_FILE_ERROR");
        return;
    }
    
    Serial.println("[RF] Replaying captured signal...");
    ELECHOUSE_cc1101.SetTx();
    
    uint8_t buffer[256];
    int totalBytes = 0;
    unsigned long startTime = millis();
    
    while (file.available() && (millis() - startTime < 30000)) {
        int len = file.read(buffer, 256);
        for (int i = 0; i < len; i++) {
            ELECHOUSE_cc1101.SendData(&buffer[i], 1);
            totalBytes++;
            delayMicroseconds(500);
        }
        vTaskDelay(5);
    }
    
    file.close();
    ELECHOUSE_cc1101.SetRx();
    
    char buf[64];
    sprintf(buf, "RF_REPLAY_DONE:%d", totalBytes);
    sendToS3(buf);
    blinkLED(2, 100);
}

// ===================== RF CAPTURE =====================
void rfCapture() {
    if (!cc1101Initialized) {
        sendToS3("RF_CAPTURE_ERR:CC1101 not initialized");
        return;
    }
    
    sendToS3("RF_CAPTURE_START");
    sendToS3("SET_FREQ:<freq> to set frequency, CAPTURE to start, STOP to exit");
    
    float currentFreq = 433.92;
    bool capturing = false;
    File captureFile;
    
    while (true) {
        if (SerialS3.available()) {
            String cmd = SerialS3.readStringUntil('\n');
            cmd.trim();
            
            if (cmd.startsWith("SET_FREQ:")) {
                currentFreq = cmd.substring(9).toFloat();
                ELECHOUSE_cc1101.setMHZ(currentFreq);
                ELECHOUSE_cc1101.SetRx();
                char buf[32];
                sprintf(buf, "FREQ_SET:%.2f", currentFreq);
                sendToS3(buf);
                
            } else if (cmd == "CAPTURE") {
                capturing = true;
                captureFile = SD.open("/rf_capture.bin", FILE_WRITE);
                if (!captureFile) {
                    sendToS3("CAPTURE_FILE_ERROR");
                    capturing = false;
                } else {
                    sendToS3("CAPTURING...");
                }
                
            } else if (cmd == "STOP") {
                if (capturing) {
                    captureFile.close();
                    sendToS3("CAPTURE_STOPPED");
                }
                break;
            }
        }
        
        if (capturing && ELECHOUSE_cc1101.CheckReceiveFlag()) {
            uint8_t buffer[64];
            uint8_t len = ELECHOUSE_cc1101.ReceiveData(buffer);
            if (len > 0 && captureFile) {
                captureFile.write(buffer, len);
                blinkLED(1, 20);
            }
        }
        vTaskDelay(10);
    }
    
    sendToS3("RF_CAPTURE_DONE");
}

// ===================== RF CUSTOM =====================
void rfCustom() {
    if (!cc1101Initialized) {
        sendToS3("RF_CUSTOM_ERR:CC1101 not initialized");
        return;
    }
    
    sendToS3("RF_CUSTOM_READY");
    Serial.println("[RF] Custom mode - waiting for commands");
    
    unsigned long start = millis();
    bool txActive = false;
    
    while (millis() - start < 30000) {
        if (SerialS3.available()) {
            String cmd = SerialS3.readStringUntil('\n');
            cmd.trim();
            
            if (cmd.startsWith("FREQ:")) {
                float freq = cmd.substring(5).toFloat();
                ELECHOUSE_cc1101.setMHZ(freq);
                char buf[32];
                sprintf(buf, "FREQ_SET:%.2f", freq);
                sendToS3(buf);
                
            } else if (cmd == "TX_START") {
                ELECHOUSE_cc1101.SetTx();
                txActive = true;
                sendToS3("TX_STARTED");
                
            } else if (cmd == "TX_STOP") {
                ELECHOUSE_cc1101.SetRx();
                txActive = false;
                sendToS3("TX_STOPPED");
                
            } else if (cmd.startsWith("SEND:")) {
                String hexData = cmd.substring(5);
                int len = hexData.length() / 2;
                uint8_t buffer[64];
                
                for (int i = 0; i < len && i < 64; i++) {
                    String byteStr = hexData.substring(i * 2, i * 2 + 2);
                    buffer[i] = (uint8_t)strtoul(byteStr.c_str(), NULL, 16);
                }
                
                ELECHOUSE_cc1101.SendData(buffer, len);
                char buf[32];
                sprintf(buf, "SENT:%d", len);
                sendToS3(buf);
                blinkLED(1, 50);
                
            } else if (cmd == "EXIT") {
                break;
            }
        }
        
        if (txActive) {
            byte data = 0xAA;
            ELECHOUSE_cc1101.SendData(&data, 1);
            vTaskDelay(1);
        }
        
        vTaskDelay(10);
    }
    
    if (txActive) ELECHOUSE_cc1101.SetRx();
    sendToS3("RF_CUSTOM_DONE");
}

// ===================== NRF24 SCAN =====================
void nrfScan() {
    if (!nrf24Initialized) {
        sendToS3("NRF_SCAN_ERR:NRF24 not initialized");
        return;
    }
    
    if (nrfScanning) {
        sendToS3("NRF_SCAN_ALREADY_RUNNING");
        return;
    }
    
    nrfScanning = true;
    sendToS3("NRF_SCAN_START");
    
    radio.startListening();
    
    for (int i = 0; i < numNrfChannels && nrfScanning; i++) {
        int ch = nrfChannels[i];
        radio.setChannel(ch);
        vTaskDelay(30);
        
        if (radio.testCarrier()) {
            char buf[64];
            sprintf(buf, "NRF_SIGNAL:channel=%d", ch);
            sendToS3(buf);
            blinkLED(1, 30);
        }
        
        if (i % 20 == 0) {
            char buf[32];
            sprintf(buf, "NRF_SCAN_PROGRESS:%d/%d", i, numNrfChannels);
            sendToS3(buf);
        }
        
        // Check for stop command
        if (SerialS3.available()) {
            String cmd = SerialS3.readStringUntil('\n');
            if (cmd == "NRF_SCAN_STOP") {
                nrfScanning = false;
                break;
            }
        }
    }
    
    nrfScanning = false;
    sendToS3("NRF_SCAN_DONE");
}

// ===================== NRF24 JAMMER =====================
void nrfJam() {
    if (!nrf24Initialized) {
        sendToS3("NRF_JAM_ERR:NRF24 not initialized");
        return;
    }
    
    nrfJamActive = !nrfJamActive;
    
    if (nrfJamActive) {
        radio.stopListening();
        sendToS3("NRF_JAM_ON");
        Serial.println("[NRF24] Jammer ACTIVATED");
    } else {
        radio.startListening();
        sendToS3("NRF_JAM_OFF");
        Serial.println("[NRF24] Jammer DEACTIVATED");
    }
}

// ===================== MOUSEJACK =====================
void mousejack() {
    if (!nrf24Initialized) {
        sendToS3("MOUSEJACK_ERR:NRF24 not initialized");
        return;
    }
    
    sendToS3("MOUSEJACK_START");
    Serial.println("[NRF24] Mousejack attack starting...");
    
    radio.stopListening();
    
    uint8_t mousejack_packet[] = {
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };
    
    int attackChannels[] = {2, 5, 8, 11, 14, 17, 20, 23, 26, 29, 32, 35, 38, 41, 44, 47, 50, 53, 56, 59, 62, 65, 68, 71, 74, 77, 80, 83};
    int numAttackChannels = sizeof(attackChannels) / sizeof(attackChannels[0]);
    
    for (int i = 0; i < numAttackChannels; i++) {
        radio.setChannel(attackChannels[i]);
        vTaskDelay(5);
        
        for (int j = 0; j < 50; j++) {
            radio.write(&mousejack_packet, sizeof(mousejack_packet));
            vTaskDelay(2);
        }
        
        char buf[32];
        sprintf(buf, "MOUSEJACK_PROGRESS:%d/%d", i + 1, numAttackChannels);
        sendToS3(buf);
        
        if (SerialS3.available()) {
            String cmd = SerialS3.readStringUntil('\n');
            if (cmd == "MOUSEJACK_STOP") break;
        }
    }
    
    radio.startListening();
    sendToS3("MOUSEJACK_DONE");
    blinkLED(3, 100);
}

// ===================== NFC READ =====================
void nfcRead() {
    if (!nfcInitialized) {
        sendToS3("NFC_READ_ERR:PN532 not initialized");
        return;
    }
    
    sendToS3("NFC_READ_START");
    Serial.println("[NFC] Waiting for tag...");
    
    uint8_t uid[7];
    uint8_t uidLen;
    
    if (nfcShield.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 3000)) {
        char buf[64];
        sprintf(buf, "NFC_UID:%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
        if (uidLen > 4) {
            sprintf(buf + 11, "%02X%02X%02X", uid[4], uid[5], uid[6]);
        }
        sendToS3(buf);
        
        if (uidLen == 4) {
            sendToS3("NFC_TYPE:MIFARE_CLASSIC");
        } else if (uidLen == 7) {
            sendToS3("NFC_TYPE:MIFARE_ULTRALIGHT");
        }
        
        blinkLED(1, 100);
    } else {
        sendToS3("NFC_NO_TAG");
    }
}

// ===================== NFC CLONE =====================
void nfcClone() {
    if (!nfcInitialized) {
        sendToS3("NFC_CLONE_ERR:PN532 not initialized");
        return;
    }
    
    sendToS3("NFC_CLONE_START");
    Serial.println("[NFC] Cloning tag...");
    
    uint8_t uid[7];
    uint8_t uidLen;
    
    if (nfcShield.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 3000)) {
        String uidStr = "NFC_CLONED_UID:";
        for (int i = 0; i < uidLen; i++) {
            if (uid[i] < 0x10) uidStr += "0";
            uidStr += String(uid[i], HEX);
        }
        sendToS3(uidStr);
        sendToS3("NFC_CLONE_COMPLETE");
        blinkLED(2, 100);
    } else {
        sendToS3("NFC_CLONE_FAIL");
    }
}

// ===================== NFC WRITE NDEF =====================
void nfcWriteNDEF() {
    if (!nfcInitialized) {
        sendToS3("NFC_WRITE_ERR:PN532 not initialized");
        return;
    }
    
    sendToS3("NFC_WRITE_NDEF_START");
    Serial.println("[NFC] Writing NDEF record...");
    // TODO: Implement NDEF write
    vTaskDelay(1000);
    sendToS3("NFC_WRITE_NDEF_DONE");
}

// ===================== NFC EMULATE =====================
void nfcEmulate() {
    if (!nfcInitialized) {
        sendToS3("NFC_EMULATE_ERR:PN532 not initialized");
        return;
    }
    
    sendToS3("NFC_EMULATE_START");
    Serial.println("[NFC] Emulating tag...");
    
    nfcShield.setPassiveActivationRetries(0xFF);
    nfcShield.SAMConfig();
    sendToS3("NFC_EMULATE_RUNNING");
    
    unsigned long start = millis();
    while (millis() - start < 30000) {
        if (SerialS3.available()) {
            String cmd = SerialS3.readStringUntil('\n');
            if (cmd == "NFC_EMULATE_STOP") break;
        }
        vTaskDelay(100);
    }
    
    sendToS3("NFC_EMULATE_STOP");
}

// ===================== AMIIBOLINK =====================
void amiibolink() {
    if (!nfcInitialized) {
        sendToS3("AMIIBOLINK_ERR:PN532 not initialized");
        return;
    }
    
    sendToS3("AMIIBOLINK_START");
    Serial.println("[NFC] Amiibolink mode...");
    // TODO: Implement Amiibolink with NTAG215
    vTaskDelay(2000);
    sendToS3("AMIIBOLINK_DONE");
}

// ===================== TV-B-GONE =====================
void tvBgone() {
    sendToS3("IR_TVBGONE_START");
    Serial.println("[IR] TV-B-Gone mode...");
    
    uint32_t tvCodes[] = {
        0x20DF10EF, 0x20DF40BF, 0x20DFC03F, 0x20DF807F,
        0x20DF609F, 0x20DF20DF, 0x20DFA05F, 0x20DFE01F,
        0x00FFA25D, 0x00FF629D, 0x00FFE21D, 0x00FF22DD,
        0x40BF10EF, 0x40BF40BF, 0x40BFC03F, 0x40BF807F
    };
    int numCodes = sizeof(tvCodes) / sizeof(tvCodes[0]);
    
    for (int i = 0; i < 150; i++) {
        for (int j = 0; j < numCodes; j++) {
            IrSender.sendNECMSB(tvCodes[j], 32);
            vTaskDelay(50);
        }
        
        if (i % 20 == 0) {
            char buf[32];
            sprintf(buf, "IR_TVBGONE_PROGRESS:%d/150", i);
            sendToS3(buf);
        }
        
        if (SerialS3.available()) {
            String cmd = SerialS3.readStringUntil('\n');
            if (cmd == "IR_STOP") break;
        }
    }
    
    sendToS3("IR_TVBGONE_DONE");
    blinkLED(2, 100);
}

// ===================== IR RECEIVE =====================
void irReceive() {
    sendToS3("IR_RECV_READY");
    Serial.println("[IR] Waiting for IR signal...");
    
    unsigned long start = millis();
    while (millis() - start < 10000) {
        if (IrReceiver.decode()) {
            char buf[128];
            
            if (IrReceiver.decodedIRData.protocol == NEC) {
                sprintf(buf, "IR_CODE:0x%08X,PROTO:NEC,BITS:%d", 
                        IrReceiver.decodedIRData.decodedRawData, 
                        IrReceiver.decodedIRData.numberOfBits);
            } else {
                sprintf(buf, "IR_CODE:0x%08X,PROTO:%d,BITS:%d", 
                        IrReceiver.decodedIRData.decodedRawData,
                        IrReceiver.decodedIRData.protocol,
                        IrReceiver.decodedIRData.numberOfBits);
            }
            sendToS3(buf);
            
            IrReceiver.resume();
            blinkLED(1, 50);
            return;
        }
        vTaskDelay(10);
    }
    
    sendToS3("IR_TIMEOUT");
}

// ===================== IR SEND NEC =====================
void irSendNEC() {
    sendToS3("IR_SEND_START");
    IrSender.sendNECMSB(0x00FFA25D, 32);
    sendToS3("IR_NEC_SENT");
    blinkLED(1, 50);
}

// ===================== IR SEND RC5 =====================
void irSendRC5() {
    sendToS3("IR_SEND_START");
    IrSender.sendRC5(0x1A, 12);
    sendToS3("IR_RC5_SENT");
    blinkLED(1, 50);
}

// ===================== IR SEND RAW =====================
void irSendRaw() {
    sendToS3("IR_SEND_RAW_START");
    // TODO: Implement raw IR send
    vTaskDelay(1000);
    sendToS3("IR_SEND_RAW_DONE");
}

// ===================== PING PONG =====================
void handlePing() {
    sendToS3("PONG");
}

// ===================== STATUS =====================
void handleStatus() {
    String status = "STATUS:";
    status += "CC1101=" + String(cc1101Initialized ? "OK" : "FAIL");
    status += ",NRF24=" + String(nrf24Initialized ? "OK" : "FAIL");
    status += ",NFC=" + String(nfcInitialized ? "OK" : "FAIL");
    status += ",RF_JAM=" + String(rfJamActive ? "ON" : "OFF");
    status += ",NRF_JAM=" + String(nrfJamActive ? "ON" : "OFF");
    status += ",SD=" + String(SD.cardType() != CARD_NONE ? "OK" : "FAIL");
    sendToS3(status);
}

// ===================== XỬ LÝ LỆNH =====================
void processCommand(String cmd) {
    cmd.trim();
    Serial.print("[S3] -> ");
    Serial.println(cmd);
    
    if (cmd == "PING") handlePing();
    else if (cmd == "STATUS") handleStatus();
    else if (cmd == "RF_SCAN") rfScan();
    else if (cmd == "RF_SCAN_STOP") rfScanning = false;
    else if (cmd == "RF_JAM_FULL") rfJamFull();
    else if (cmd == "RF_REPLAY") rfReplay();
    else if (cmd == "RF_CAPTURE") rfCapture();
    else if (cmd == "RF_CUSTOM") rfCustom();
    else if (cmd == "NRF_SCAN") nrfScan();
    else if (cmd == "NRF_SCAN_STOP") nrfScanning = false;
    else if (cmd == "NRF24_JAM") nrfJam();
    else if (cmd == "MOUSEJACK") mousejack();
    else if (cmd == "MOUSEJACK_STOP") { /* handled in function */ }
    else if (cmd == "NFC_READ") nfcRead();
    else if (cmd == "NFC_CLONE") nfcClone();
    else if (cmd == "NFC_WRITE_NDEF") nfcWriteNDEF();
    else if (cmd == "NFC_EMULATE") nfcEmulate();
    else if (cmd == "NFC_EMULATE_STOP") { /* handled in function */ }
    else if (cmd == "AMIIBOLINK") amiibolink();
    else if (cmd == "IR_TVBGONE") tvBgone();
    else if (cmd == "IR_STOP") { /* handled in function */ }
    else if (cmd == "IR_RECV") irReceive();
    else if (cmd == "IR_SEND_NEC") irSendNEC();
    else if (cmd == "IR_SEND_RC5") irSendRC5();
    else if (cmd == "IR_SEND_RAW") irSendRaw();
    else if (cmd == "HELP") {
        sendToS3("HELP: PING, STATUS, RF_SCAN, RF_JAM_FULL, RF_REPLAY, RF_CAPTURE, RF_CUSTOM, NRF_SCAN, NRF24_JAM, MOUSEJACK, NFC_READ, NFC_CLONE, NFC_WRITE_NDEF, NFC_EMULATE, AMIIBOLINK, IR_TVBGONE, IR_RECV, IR_SEND_NEC, IR_SEND_RC5");
    }
    else if (cmd != "") {
        sendToS3("ERR: Unknown command: " + cmd);
    }
}

// ===================== JAMMER LOOP =====================
void handleJammers() {
    // RF Jammer
    if (rfJamActive && cc1101Initialized) {
        static unsigned long lastRfJam = 0;
        if (millis() - lastRfJam > 2) {
            byte jamData[16];
            for (int i = 0; i < 16; i++) jamData[i] = random(0xFF);
            ELECHOUSE_cc1101.SendData(jamData, 16);
            lastRfJam = millis();
        }
    }
    
    // NRF24 Jammer
    if (nrfJamActive && nrf24Initialized) {
        static int jamChannel = 0;
        static unsigned long lastNrfJam = 0;
        if (millis() - lastNrfJam > 5) {
            jamChannel = (jamChannel + 1) % numNrfChannels;
            radio.setChannel(nrfChannels[jamChannel]);
            uint8_t junk[32];
            memset(junk, 0xFF, 32);
            radio.write(&junk, 32);
            lastNrfJam = millis();
        }
    }
}

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    SerialS3.begin(UART_BAUD, SERIAL_8N1, UART_S3_RX, UART_S3_TX);
    
    pinMode(STATUS_LED, OUTPUT);
    blinkLED(2, 100);
    
    Serial.println("\n========================================");
    Serial.println("ESP32-C5 RF Controller v2.0 (FIXED)");
    Serial.println("========================================");
    
    // FIX: Khởi tạo CC1101 TRƯỚC để set SPI pins
    cc1101Initialized = initCC1101();
    
    // FIX: SAU ĐÓ mới khởi tạo SPI
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, NRF_CSN);
    Serial.println("[SPI] Bus initialized");
    
    // FIX: Khởi tạo SD với chân CS riêng
    if (!SD.begin(SD_CS)) {
        Serial.println("[SD] Card init FAILED (normal if no card)");
    } else {
        Serial.printf("[SD] Card OK - Type: %d, Size: %lluMB\n", 
                      SD.cardType(), SD.cardSize() / (1024 * 1024));
    }
    
    // Khởi tạo các module còn lại
    nrf24Initialized = initNRF24();
    nfcInitialized = initNFC();
    initIR();
    
    // Gửi tín hiệu sẵn sàng
    vTaskDelay(500);
    sendToS3("C5_READY");
    blinkLED(3, 100);
    
    Serial.println("\n[C5] System READY!");
    Serial.println("[C5] Waiting for commands from ESP32-S3...");
    handleStatus();
}

// ===================== LOOP =====================
void loop() {
    // Đọc lệnh từ ESP32-S3
    while (SerialS3.available()) {
        char c = SerialS3.read();
        if (c == '\n') {
            if (commandBuffer.length() > 0) {
                processCommand(commandBuffer);
                commandBuffer = "";
            }
        } else if (c != '\r') {
            commandBuffer += c;
        }
    }
    
    // Xử lý jammers
    handleJammers();
    
    vTaskDelay(5);
}
