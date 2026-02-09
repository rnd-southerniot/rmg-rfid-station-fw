#pragma once
#include <Arduino.h>

void ntpInit();
bool ntpIsSynced();
String ntpGetIsoTimestamp();
String ntpGetTimeStr();  // "HH:MM" for status bar
