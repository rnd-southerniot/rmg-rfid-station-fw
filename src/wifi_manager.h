#pragma once
#include <Arduino.h>

void wifiInit();
bool wifiIsConnected();
void wifiReconnect();
String wifiGetMac();
int8_t wifiGetRssi();   // 0 when not connected
