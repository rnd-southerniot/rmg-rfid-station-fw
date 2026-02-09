#pragma once
#include <Arduino.h>

void rfidInit();
bool rfidCardPresent();
String rfidReadUid();
uint8_t rfidGetVersion();  // Returns MFRC522 version register (0x91/0x92 = OK)
