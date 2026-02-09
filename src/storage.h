#pragma once
#include <Arduino.h>

void storageInit();
void storageSaveToken(const String& token);
String storageLoadToken();
void storageClearToken();

void storageSaveStationInfo(const String& stationId, const String& lineId, const String& type);
String storageLoadStationId();
String storageLoadLineId();
String storageLoadType();
