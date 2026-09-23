#pragma once
#include <Arduino.h>

void ntpInit();
bool ntpIsSynced();
String ntpGetIsoTimestamp();
String ntpGetTimeStr();  // "HH:MM" for status bar
uint32_t ntpGetEpoch();  // seconds UTC, 0 if never synced (non-blocking)
