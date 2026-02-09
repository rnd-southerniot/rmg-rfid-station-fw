#include "touch.h"
#include "config.h"
#include "log.h"
#include <Wire.h>

// FT6336 register addresses
#define FT_REG_NUM_TOUCHES 0x02
#define FT_REG_P1_XH       0x03
#define FT_REG_P1_XL       0x04
#define FT_REG_P1_YH       0x05
#define FT_REG_P1_YL       0x06

void touchInit() {
    Wire.begin(TOUCH_SDA, TOUCH_SCL);

    // Reset touch controller
    if (TOUCH_RST >= 0) {
        pinMode(TOUCH_RST, OUTPUT);
        digitalWrite(TOUCH_RST, LOW);
        delay(10);
        digitalWrite(TOUCH_RST, HIGH);
        delay(300);
    }

    // Check if touch controller responds
    Wire.beginTransmission(TOUCH_ADDR);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
        LOG_I("[Touch] FT6336 initialized\n");
    } else {
        LOG_E("[Touch] FT6336 not found (error %d)\n", err);
    }
}

bool touchIsConnected() {
    Wire.beginTransmission(TOUCH_ADDR);
    return Wire.endTransmission() == 0;
}

bool touchGetPoint(int& x, int& y) {
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(FT_REG_NUM_TOUCHES);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)5);

    if (Wire.available() < 5) return false;

    uint8_t touches = Wire.read() & 0x0F;
    if (touches == 0) return false;

    uint8_t xh = Wire.read();
    uint8_t xl = Wire.read();
    uint8_t yh = Wire.read();
    uint8_t yl = Wire.read();

    int rawX = ((xh & 0x0F) << 8) | xl;
    int rawY = ((yh & 0x0F) << 8) | yl;

    // Transform to landscape coordinates (320x240)
    // Based on calibration data from reference:
    // Portrait raw (x,y) → landscape screen (sx,sy)
    // The touch controller reports in portrait orientation
    x = rawY;                    // Portrait Y → Landscape X
    y = LCD_HEIGHT - 1 - rawX;   // Portrait X → Landscape Y (inverted)

    // Clamp
    if (x < 0) x = 0;
    if (x >= LCD_WIDTH) x = LCD_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= LCD_HEIGHT) y = LCD_HEIGHT - 1;

    return true;
}

QcChoice touchCheckQcButton(int x, int y) {
    // PASS button region: x=10..155, y=80..200
    if (x >= 10 && x <= 155 && y >= 80 && y <= 200) {
        return QC_PASS_TOUCH;
    }
    // FAIL button region: x=165..310, y=80..200
    if (x >= 165 && x <= 310 && y >= 80 && y <= 200) {
        return QC_FAIL_TOUCH;
    }
    return QC_NONE;
}
