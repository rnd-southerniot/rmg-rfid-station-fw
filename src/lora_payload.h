/*
 * lora_payload.h — LoRaWAN uplink payload encoders for the RMG RFID station (schema 1).
 *
 * Pure code: no Arduino dependencies, compiled for the target and for host-native unit tests.
 * The byte layout is the contract with the ChirpStack codec in tools/chirpstack/ and with the
 * ETS backend; it is documented in docs/LORAWAN_PAYLOAD.md. All multi-byte fields big-endian.
 *
 * fPort 10 — scan event (10 + uid_len [+ 1 + op_len] bytes, never more than 24):
 *   [0]   schema = 1
 *   [1]   flags  b0 TIME_SYNCED (epoch valid) b1 QC_STATION b2 OPERATOR_PRESENT b3 REPLAY
 *   [2..5] epoch  u32 seconds UTC, 0 when NTP never synced
 *   [6..7] seq    u16 per-station counter (same value as in the HTTP event_id "E_<epoch>_<seq>")
 *   [8]   event_type 1 COMPLETE, 2 QC_PASS, 3 QC_FAIL
 *   [9]   uid_len 4..10
 *   [10..] uid bytes — exactly the bytes the HTTP bundle.rfid_uid hex string encodes
 *   [10+uid_len]  op_len      only when OPERATOR_PRESENT
 *   [11+uid_len..] operator badge UID bytes
 *
 * fPort 11 — offline heartbeat (20 bytes):
 *   [0] schema=1  [1] flags b0 TIME_SYNCED b1 OPERATOR_LOGGED_IN b2 STATION_MAPPED b3 WIFI_ASSOCIATED
 *   [2..5] uptime_s u32  [6..7] queued_http_events u16  [8..9] last_seq u16  [10] lora_dropped u8
 *   [11..13] fw major/minor/patch  [14] wifi_rssi i8 (0 = unknown)  [15] offline_reason
 *   [16..19] epoch u32
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LORA_SCHEMA_VERSION     1u
#define LORA_FPORT_SCAN         10u
#define LORA_FPORT_HEARTBEAT    11u
#define LORA_PAYLOAD_MAX        24u
#define LORA_HEARTBEAT_LEN      20u
#define LORA_UID_MIN            4u
#define LORA_UID_MAX            10u

enum {
    LORA_EVT_COMPLETE = 1,
    LORA_EVT_QC_PASS  = 2,
    LORA_EVT_QC_FAIL  = 3
};

enum {
    LORA_SCAN_FLAG_TIME_SYNCED = 1u << 0,   /* derived by the encoder from epoch != 0 */
    LORA_SCAN_FLAG_QC_STATION  = 1u << 1,   /* caller-supplied */
    LORA_SCAN_FLAG_OPERATOR    = 1u << 2,   /* derived by the encoder when the trailer fits */
    LORA_SCAN_FLAG_REPLAY      = 1u << 3    /* caller-supplied */
};

enum {
    LORA_HB_FLAG_TIME_SYNCED        = 1u << 0,   /* derived from epoch != 0 */
    LORA_HB_FLAG_OPERATOR_LOGGED_IN = 1u << 1,
    LORA_HB_FLAG_STATION_MAPPED     = 1u << 2,
    LORA_HB_FLAG_WIFI_ASSOCIATED    = 1u << 3
};

enum {
    LORA_OFFLINE_NONE               = 0,
    LORA_OFFLINE_WIFI_DOWN          = 1,
    LORA_OFFLINE_SERVER_UNREACHABLE = 2,
    LORA_OFFLINE_BOOT_NO_WIFI       = 3
};

typedef struct {
    uint32_t epoch;
    uint16_t seq;
    uint8_t  event_type;             /* LORA_EVT_* */
    uint8_t  flags;                  /* only QC_STATION / REPLAY are honoured from the caller */
    uint8_t  uid_len;                /* 4..10 */
    uint8_t  uid[LORA_UID_MAX];
    uint8_t  op_len;                 /* 0 = no operator; > LORA_UID_MAX is treated as 0 */
    uint8_t  op[LORA_UID_MAX];
} lora_scan_event_t;

typedef struct {
    uint32_t uptime_s;
    uint16_t queued_http_events;
    uint16_t last_seq;
    uint8_t  lora_dropped;
    uint8_t  fw[3];
    int8_t   wifi_rssi;
    uint8_t  offline_reason;         /* LORA_OFFLINE_* */
    uint32_t epoch;
    uint8_t  flags;                  /* OPERATOR_LOGGED_IN / STATION_MAPPED / WIFI_ASSOCIATED */
} lora_heartbeat_t;

/* Returns the encoded length, or 0 if the event is invalid (bad uid_len / event_type) or cap too small. */
size_t lora_encode_scan(const lora_scan_event_t* ev, uint8_t* out, size_t cap);

/* Returns LORA_HEARTBEAT_LEN, or 0 if cap is too small. */
size_t lora_encode_heartbeat(const lora_heartbeat_t* hb, uint8_t* out, size_t cap);

/*
 * Parse an even-length hex string (either case, no separators) into bytes.
 * Returns false on odd length, non-hex characters, empty input, or more than `cap` bytes.
 */
bool lora_hex_to_bytes(const char* hex, uint8_t* out, uint8_t cap, uint8_t* len);

/* Parse "M.m.p" into three bytes; returns false if the string is not three dotted decimals <= 255. */
bool lora_fw_version_bytes(const char* ver, uint8_t out[3]);

#ifdef __cplusplus
}
#endif
