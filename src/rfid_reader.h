#pragma once
#include <Arduino.h>

void rfidInit();
bool rfidCardPresent();
String rfidReadUid();
