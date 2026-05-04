#pragma once
#include <Arduino.h>

void displayInit();
void displayBootScreen(const String& status);
void displayClaimingScreen();
void displayUnmappedScreen(const String& mac);
void displayReadyScreen(const String& stationId, const String& lineName, const String& type);
void displayLoginScreen(const String& stationId);
void displayScanResult(const String& rfidUid, const String& eventType, bool success, const String& message);
void displayQcButtons(const String& rfidUid);
void displayError(const String& message);
void displayPostScreen();
void displayPostResult(int line, const String& label, bool pass);
void displayStatusBar(bool wifiOk, const String& stationId, const String& timeStr);
void displayClear();
