// Pin map for the Freenove ESP32-S3 Display FNK0115Q (5.0" 800x480 IPS, capacitive touch).
// Taken from Freenove's schematics and example sketches. The 4.3" IPS (FNK0115L) uses the
// same display and touch pins and timing but has no speaker amp; see AUDIO below.
#pragma once

// ---- Display (16-bit parallel RGB panel) ----
#define LCD_WIDTH   800
#define LCD_HEIGHT  480
#define LCD_PCLK_HZ 13000000

#define PIN_LCD_DE    40
#define PIN_LCD_VSYNC 41
#define PIN_LCD_HSYNC 39
#define PIN_LCD_PCLK  42
#define PIN_LCD_R0 45
#define PIN_LCD_R1 48
#define PIN_LCD_R2 47
#define PIN_LCD_R3 21
#define PIN_LCD_R4 14
#define PIN_LCD_G0 5
#define PIN_LCD_G1 6
#define PIN_LCD_G2 7
#define PIN_LCD_G3 15
#define PIN_LCD_G4 16
#define PIN_LCD_G5 4
#define PIN_LCD_B0 8
#define PIN_LCD_B1 3
#define PIN_LCD_B2 46
#define PIN_LCD_B3 9
#define PIN_LCD_B4 1

// Backlight: drives the EN/PWM pin of the SY7200 LED driver, so PWM dims the screen.
#define PIN_BACKLIGHT 2

// ---- I2C bus (shared by the GT911 touch chip and the SHT4x sensor) ----
// Both are on the board's 4-pin I2C header.
#define PIN_I2C_SDA 19
#define PIN_I2C_SCL 20

// ---- Touch (GT911) ----
// INT is only used while the GT911 starts up; afterwards the pin is reused for audio.
#define PIN_TOUCH_INT 18
#define PIN_TOUCH_RST 38

// ---- Audio (step 2) ----
// 5" board: on-board NS4168 I2S amp driving the speaker connector (Freenove's pins).
// BCLK shares IO0 with the BOOT button, so BOOT can't be used as an input.
#define PIN_I2S_BCLK 0
#define PIN_I2S_LRCK 18
#define PIN_I2S_DOUT 17

// (Snooze is on screen; there's no physical button.)
// The only free GPIOs are IO11, IO12 and IO13 on the "SPI" header P3 (its pin 1 is IO19, the
// I2C data line, not a spare). They're shared with the SD slot, so leave it empty; sounds
// live in flash. The SD slot's resistor network RN1 pulls IO11 and IO13 (not IO12) up to
// 3.3 V. IO12 and IO13 are unused. There's no battery: the clock runs from USB, and after a
// power cut it gets the time back from the internet and rings any alarm it missed (alarm.cpp).

// ---- Optional AM2302 (DHT22) sensor, used if no SHT3x/SHT4x is found on I2C ----
// Data wire to IO11 (P3). IO11 already has a pull-up from the SD slot.
#define PIN_AM2302 11
