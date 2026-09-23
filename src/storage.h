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

// Per-station event sequence (u16, wraps). Shared by the HTTP event_id "E_<epoch>_<seq>" and the
// LoRa scan payload so the backend can de-duplicate. Checkpointed to NVS every 64 values and
// jumped ahead by 64 at boot, so a value is never handed out twice across reboots.
uint16_t storageNextSeq();
uint16_t storageCurrentSeq();
