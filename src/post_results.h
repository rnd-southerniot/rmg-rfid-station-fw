#pragma once
// Power-on self-test results, recorded by handleBoot() and printed by the bench console
// (`sys info`) because the boot log on the S3's native USB port is easy to miss.
#include <stdint.h>

struct PostResults {
    bool     valid;      // set once the POST has run
    bool     lcd;
    uint16_t lcdId;      // ILI9341 RDID4 (expect 0x9341); 0 = not read on this board
    bool     led;
    bool     buzzer;
    bool     rfid;
    uint8_t  rfidVer;
    bool     touch;
    bool     lora;
};

extern PostResults g_post;
