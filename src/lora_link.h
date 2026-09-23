#pragma once
/*
 * lora_link.h — LoRaWAN offline-fallback link (SX1262 via RadioLib) for the RAK3212 build.
 *
 * WiFi/HTTP stays the primary transport. When an HTTP event POST fails, main.cpp queues the
 * event for HTTP replay (event_queue) AND hands a copy to this module, which sends it as a
 * compact fPort-10 uplink; while the link is down a fPort-11 heartbeat goes out periodically.
 *
 * All RadioLib and NVS("lorawan") calls run on a dedicated FreeRTOS task (core 0) because
 * sendReceive() blocks for the TX + RX1/RX2 windows (~2.4-3.3 s) and an OTAA join for ~6-7 s.
 * The loop() side only talks through a FreeRTOS queue and a spinlock-guarded status copy.
 *
 * Boards without a radio (BOARD_HAS_LORA == 0) get inline no-op stubs so main.cpp needs no
 * #ifdefs and generates no extra code.
 */
#include <Arduino.h>
#include "config.h"
#include "lora_payload.h"

enum LoraState : uint8_t {
    LORA_DISABLED = 0,   // no radio on this board / loraInit() never ran
    LORA_RADIO_FAIL,     // SX1262 did not answer on SPI
    LORA_NO_CREDS,       // AppKey is all zero: radio idle
    LORA_JOINING,        // OTAA join in progress
    LORA_BACKOFF,        // join failed, waiting before the next attempt
    LORA_JOINED          // session active, uplinks allowed
};

// Compact state for the display status bar.
enum LoraUiState : uint8_t { LORA_UI_NONE = 0, LORA_UI_JOINING, LORA_UI_JOINED, LORA_UI_FAIL };

struct LoraStatus {
    LoraState state;
    uint64_t  devEui;
    uint64_t  joinEui;
    bool      credsPresent;
    bool      devEuiFromMac;
    bool      linkDown;
    uint8_t   offlineReason;     // LORA_OFFLINE_*
    uint32_t  joinAttempts;
    uint32_t  uplinksOk;
    uint32_t  uplinksFail;
    uint32_t  dropped;           // LoRa copies discarded because the queue was full
    uint32_t  fcntUp;
    int16_t   lastRc;            // last RadioLib return code
    uint32_t  lastUplinkMs;
    uint8_t   queued;            // commands waiting for the task
    uint32_t  taskStackFree;     // bytes, high-water mark (0 = task not running)
};

// Snapshot of application state the offline heartbeat reports; main.cpp refreshes it.
struct LoraAppInfo {
    uint16_t queuedHttpEvents;
    uint16_t lastSeq;
    bool     operatorLoggedIn;
    bool     stationMapped;
    bool     wifiAssociated;
    int8_t   wifiRssi;           // 0 = unknown
};

#if BOARD_HAS_LORA

// Synchronous radio probe (~100 ms) then starts the task. Returns true if the SX1262 answered.
bool loraInit();

// Queue a scan event copy (encoded on the caller's side). Returns false if not joined / invalid.
bool loraEnqueueScan(const String& uidHex, const String& eventType, uint32_t epoch, uint16_t seq,
                     bool qcStation, bool replay, const String& operatorUidHex);

void        loraUpdateAppInfo(const LoraAppInfo& info);
void        loraSetLinkDown(bool down, uint8_t reason);   // idempotent; a down-transition queues a heartbeat
void        loraRequestJoin();
void        loraRequestHeartbeat();
void        loraClearSession();                           // wipes nonces + session, then re-joins
LoraStatus  loraGetStatus();
bool        loraIsJoined();
LoraUiState loraUiState();
const char* loraStateName(LoraState s);

#else

static inline bool loraInit() { return false; }
static inline bool loraEnqueueScan(const String&, const String&, uint32_t, uint16_t, bool, bool, const String&) { return false; }
static inline void loraUpdateAppInfo(const LoraAppInfo&) {}
static inline void loraSetLinkDown(bool, uint8_t) {}
static inline void loraRequestJoin() {}
static inline void loraRequestHeartbeat() {}
static inline void loraClearSession() {}
static inline LoraStatus loraGetStatus() { LoraStatus s = {}; return s; }
static inline bool loraIsJoined() { return false; }
static inline LoraUiState loraUiState() { return LORA_UI_NONE; }
static inline const char* loraStateName(LoraState) { return "disabled"; }

#endif
