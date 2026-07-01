#pragma once

// ---------------------------------------------------------------------------
// Guition JC1060P470C 7" (ESP32-P4) pin map
// ---------------------------------------------------------------------------

// Panel resolution (JD9165 MIPI-DSI)
#define LCD_H_RES 1024
#define LCD_V_RES 600

// JD9165 panel control
#define LCD_RST 27   // panel reset
#define LCD_LED 23   // backlight enable / PWM

// GT911 touch controller (native IDF I2C bus)
#define TP_I2C_SDA 7
#define TP_I2C_SCL 8
#define TP_RST     -1   // not wired on this board
#define TP_INT     -1   // not wired on this board
