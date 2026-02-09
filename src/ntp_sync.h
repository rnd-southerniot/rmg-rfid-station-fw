#pragma once
#include <Arduino.h>

void ntpInit();
bool ntpIsSynced();
String ntpGetIsoTimestamp();
