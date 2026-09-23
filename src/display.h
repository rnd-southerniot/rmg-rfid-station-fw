#pragma once
#include <Arduino.h>
#include "lora_link.h"   // LoraUiState for the status bar

void displayInit();
uint16_t displayReadId();   // ILI9341 RDID4 over MISO: 0x9341 expected, 0x0000/0xFFFF = no read-back
void displayBootScreen(const String& status);
void displayClaimingScreen();
void displayUnmappedScreen(const String& mac);
void displayReadyScreen(const String& stationId, const String& lineName, const String& type);
void displayLoginScreen(const String& stationId, bool offlineNote = false);
void displayScanResult(const String& rfidUid, const String& eventType, bool success, const String& message);
void displayQcButtons(const String& rfidUid);
void displayError(const String& message);
void displayPostScreen();
void displayPostResult(int line, const String& label, bool pass);
void displayStatusBar(bool wifiOk, const String& stationId, const String& timeStr, LoraUiState lora = LORA_UI_NONE);
void displayClear();
