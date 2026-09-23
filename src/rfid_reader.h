#pragma once
#include <Arduino.h>

void rfidInit();
bool rfidCardPresent();
String rfidReadUid();
// Presence/identity probe used by the boot self-test. 0x00 or 0xFF = no reader.
//   MFRC522 backend (esp32dev): the VersionReg value (0x91/0x92 = genuine chip)
//   UART backend (rak3212):     bit0 = RX line idled HIGH at init, bit1 = a valid frame seen
uint8_t rfidGetVersion();
