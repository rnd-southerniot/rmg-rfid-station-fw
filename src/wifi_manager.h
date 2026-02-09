#pragma once
#include <Arduino.h>

void wifiInit();
bool wifiIsConnected();
void wifiReconnect();
String wifiGetMac();
