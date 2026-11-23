// PicoCalc hardware pin assignments (Pico 2 W module)
#pragma once

// LCD (ST7365P, 320x320, SPI1)
#define LCD_SPI         spi1
#define LCD_PIN_SCK     10
#define LCD_PIN_MOSI    11
#define LCD_PIN_MISO    12
#define LCD_PIN_CS      13
#define LCD_PIN_DC      14
#define LCD_PIN_RST     15
#define LCD_BAUD        25000000

// South bridge (STM32 keyboard/battery controller, I2C1)
#define SB_I2C          i2c1
#define SB_PIN_SDA      6
#define SB_PIN_SCL      7
#define SB_ADDR         0x1F
#define SB_BAUD         10000

// SD card (SPI0)
#define SD_SPI          spi0
#define SD_PIN_MISO     16
#define SD_PIN_CS       17
#define SD_PIN_SCK      18
#define SD_PIN_MOSI     19
#define SD_INIT_BAUD    400000
#define SD_BAUD         25000000
