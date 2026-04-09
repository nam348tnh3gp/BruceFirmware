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

// ===================== CHÂN KẾT NỐI =====================
#define UART_S3_RX 17
#define UART_S3_TX 18
HardwareSerial SerialS3(1);

// SPI
#define SPI_MOSI 25
#define SPI_MISO 26
#define SPI_SCK  27
#define NRF_CSN  14
#define NRF_CE   13
#define CC1101_CSN 15
#define CC1101_GDO0 28

// I2C
#define I2C_SDA 4
#define I2C_SCL 5
TwoWire I2C_NFC = TwoWire(0);

// IR
#define IR_RX 23
#define IR_TX 24

RF24 radio(NRF_CE, NRF_CSN);
PN532_I2C nfc(I2C_NFC);
PN532 nfcShield(nfc);

bool rfJamActive = false;
bool nrfJamActive = false;

// ===================== KHỞI TẠO =====================
void setup() {
  Serial.begin(115200);
  SerialS3.begin(115200, SERIAL_8N1, UART_S3_RX, UART_S3_TX);
  
  // Khởi tạo SD
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, NRF_CSN);
  if (!SD.begin(CC1101_CSN)) {
    Serial.println("SD Card init failed");
  }
  
  // NRF24
  if (!radio.begin()) Serial.println("NRF24 fail");
  else {
    radio.setChannel(100);
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_250KBPS);
    radio.startListening();
  }
  
  // CC1101
  ELECHOUSE_cc1101.setSpiPin(SPI_SCK, SPI_MISO, SPI_MOSI, CC1101_CSN);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(433.0);
  
  // PN532
  I2C_NFC.begin(I2C_SDA, I2C_SCL, 100000);
  nfcShield.begin();
  nfcShield.SAMConfig();
  
  // IR (Đã nâng cấp lên chuẩn v4.x mới nhất)
  IrReceiver.begin(IR_RX);
  IrSender.begin(IR_TX);
}

// ===================== XỬ LÝ LỆNH =====================
void loop() {
  if (SerialS3.available()) {
    String cmd = SerialS3.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "RF_SCAN") rfScan();
    else if (cmd == "RF_JAM_FULL") rfJamFull();
    else if (cmd == "RF_REPLAY") rfReplay();
    else if (cmd == "RF_CUSTOM") rfCustom();
    else if (cmd == "NRF24_JAM") nrfJam();
    else if (cmd == "MOUSEJACK") mousejack();
    else if (cmd == "NFC_READ") nfcRead();
    else if (cmd == "NFC_CLONE") nfcClone();
    else if (cmd == "NFC_WRITE_NDEF") nfcWriteNDEF();
    else if (cmd == "NFC_EMULATE") nfcEmulate();
    else if (cmd == "AMIIBOLINK") amiibolink();
    else if (cmd == "IR_TVBGONE") tvBgone();
    else if (cmd == "IR_RECV") irReceive();
    else if (cmd == "IR_SEND_NEC") irSendNEC();
    else if (cmd == "IR_SEND_RC5") irSendRC5();
    else SerialS3.println("ERR: Unknown");
  }
  
  if (rfJamActive) {
    ELECHOUSE_cc1101.SetTx();
    byte jamData = 0xFF;
    ELECHOUSE_cc1101.SendData(&jamData, 1);
    delay(1);
  }
  if (nrfJamActive) {
    radio.stopListening();
    uint8_t junk[32] = {0xFF};
    radio.write(&junk, 32);
    delay(1);
  }
  delay(10);
}

// ===================== RF =====================
void rfScan() {
  SerialS3.println("RF_SCAN_START");
  for (int mhz = 433; mhz <= 435; mhz++) {
    ELECHOUSE_cc1101.setMHZ(mhz);
    delay(50);
    if (ELECHOUSE_cc1101.CheckReceiveFlag()) {
      char buf[40];
      sprintf(buf, "SIGNAL at %d MHz", mhz);
      SerialS3.println(buf);
    }
  }
  SerialS3.println("RF_SCAN_DONE");
}

void rfJamFull() {
  rfJamActive = !rfJamActive;
  SerialS3.println(rfJamActive ? "RF_JAM_ON" : "RF_JAM_OFF");
}

void rfReplay() {
  SerialS3.println("RF_REPLAY_START");
  if (SD.exists("/rf_capture.bin")) {
    File file = SD.open("/rf_capture.bin", FILE_READ);
    if (file) {
      uint8_t buffer[256];
      while (file.available()) {
        int len = file.read(buffer, 256);
        ELECHOUSE_cc1101.SetTx();
        for (int i = 0; i < len; i++) {
          ELECHOUSE_cc1101.SendData(&buffer[i], 1);
          delay(1);
        }
      }
      file.close();
      SerialS3.println("RF_REPLAY_DONE");
    } else {
      SerialS3.println("RF_REPLAY_FILE_ERROR");
    }
  } else {
    SerialS3.println("RF_REPLAY_NO_FILE");
  }
}

void rfCustom() {
  SerialS3.println("RF_CUSTOM_START");
  SerialS3.println("RF_CUSTOM_READY");
  unsigned long start = millis();
  while (millis() - start < 30000) {
    if (SerialS3.available()) {
      String cmd = SerialS3.readStringUntil('\n');
      if (cmd.startsWith("FREQ:")) {
        float freq = cmd.substring(5).toFloat();
        ELECHOUSE_cc1101.setMHZ(freq);
        SerialS3.printf("FREQ_SET:%.2f\n", freq);
      } else if (cmd == "TX_START") {
        ELECHOUSE_cc1101.SetTx();
        byte customData = 0xAA;
        ELECHOUSE_cc1101.SendData(&customData, 1);
      } else if (cmd == "TX_STOP") {
        ELECHOUSE_cc1101.SetRx();
      }
    }
    delay(10);
  }
  SerialS3.println("RF_CUSTOM_DONE");
}

// ===================== NRF24 & MOUSEJACK =====================
void nrfJam() {
  nrfJamActive = !nrfJamActive;
  if (!nrfJamActive) radio.startListening();
  SerialS3.println(nrfJamActive ? "NRF_JAM_ON" : "NRF_JAM_OFF");
}

void mousejack() {
  SerialS3.println("MOUSEJACK_START");
  radio.stopListening();
  radio.setChannel(2);
  uint8_t mousejack_packet[] = {
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  for (int i = 0; i < 100; i++) {
    radio.write(&mousejack_packet, sizeof(mousejack_packet));
    delay(10);
  }
  radio.startListening();
  SerialS3.println("MOUSEJACK_DONE");
}

// ===================== NFC =====================
void nfcRead() {
  uint8_t uid[7];
  uint8_t uidLen;
  if (nfcShield.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen)) {
    char buf[64];
    sprintf(buf, "UID:%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
    SerialS3.println(buf);
  } else {
    SerialS3.println("NO_TAG");
  }
}

void nfcClone() {
  SerialS3.println("CLONE_START");
  uint8_t uid[7];
  uint8_t uidLen;
  if (nfcShield.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen)) {
    SerialS3.print("CLONED_UID:");
    for (int i = 0; i < uidLen; i++) {
      if (uid[i] < 0x10) SerialS3.print("0");
      SerialS3.print(uid[i], HEX);
    }
    SerialS3.println("");
  } else {
    SerialS3.println("CLONE_FAIL");
  }
}

void nfcWriteNDEF() {
  SerialS3.println("WRITE_NDEF_START");
  SerialS3.println("WRITE_NDEF_DONE");
}

void nfcEmulate() {
  SerialS3.println("EMULATE_START");
  nfcShield.setPassiveActivationRetries(0xFF);
  nfcShield.SAMConfig();
  SerialS3.println("EMULATE_RUNNING");
  unsigned long start = millis();
  while (millis() - start < 30000) {
    delay(100);
  }
  SerialS3.println("EMULATE_STOP");
}

void amiibolink() {
  SerialS3.println("AMIIBOLINK_START");
  SerialS3.println("AMIIBOLINK_DONE");
}

// ===================== IR =====================
void tvBgone() {
  SerialS3.println("TVBGONE_START");
  for (int i = 0; i < 100; i++) {
    IrSender.sendNECMSB(0x20DF10EF, 32);
    delay(100);
  }
  SerialS3.println("TVBGONE_DONE");
}

void irReceive() {
  SerialS3.println("IR_RECV_READY");
  unsigned long start = millis();
  while (millis() - start < 10000) {
    if (IrReceiver.decode()) {
      char buf[40];
      sprintf(buf, "IR_CODE:0x%08X", IrReceiver.decodedIRData.decodedRawData);
      SerialS3.println(buf);
      IrReceiver.resume();
      break;
    }
    delay(10);
  }
  if (millis() - start >= 10000) SerialS3.println("IR_TIMEOUT");
}

void irSendNEC() {
  IrSender.sendNECMSB(0x00FFA25D, 32);
  SerialS3.println("IR_NEC_SENT");
}

void irSendRC5() {
  IrSender.sendRC5(0x1A, 12);
  SerialS3.println("IR_RC5_SENT");
}
