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

// Operator JWT (issued by /auth/login). Persisted across reboots so the
// operator does not have to re-tap their badge on every power cycle.
void   storageSaveJwt(const String& jwt);
String storageLoadJwt();
void   storageClearJwt();
