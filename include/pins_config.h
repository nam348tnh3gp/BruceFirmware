#ifndef PINS_CONFIG_H
#define PINS_CONFIG_H

// ==================== ESP32-S3 PINS ====================
// Màn hình ST7789 (SPI)
#define TFT_MOSI    11
#define TFT_SCLK    12
#define TFT_CS      10
#define TFT_DC      14
#define TFT_RST     15
#define TFT_BL      21

// SD Card (dùng chung SPI với màn hình)
#define SD_CS       9
#define SD_MISO     13

// Phím 5 chiều
#define BTN_UP      1
#define BTN_DOWN    2
#define BTN_LEFT    3
#define BTN_RIGHT   38
#define BTN_CENTER  39

// UART với ESP32-C5
#define UART_C5_TX  17
#define UART_C5_RX  18

// ==================== ESP32-C5 PINS ====================
// SPI chung cho NRF24 & CC1101
#define SPI_MOSI    25
#define SPI_MISO    26
#define SPI_SCK     27

// NRF24
#define NRF_CE      13
#define NRF_CSN     14

// CC1101
#define CC1101_CSN  15
#define CC1101_GDO0 28

// NFC PN532 (I2C)
#define PN532_SDA   4
#define PN532_SCL   5

// IR
#define IR_RX       23
#define IR_TX       24

#endif
