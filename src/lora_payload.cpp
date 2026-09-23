/*
 * lora_payload.cpp — see lora_payload.h. Pure C-style code; no Arduino, no heap, no I/O.
 */
#include "lora_payload.h"

#include <string.h>

static void put_u32(uint8_t* out, uint32_t v)
{
    out[0] = (uint8_t)(v >> 24);
    out[1] = (uint8_t)(v >> 16);
    out[2] = (uint8_t)(v >> 8);
    out[3] = (uint8_t)(v);
}

static void put_u16(uint8_t* out, uint16_t v)
{
    out[0] = (uint8_t)(v >> 8);
    out[1] = (uint8_t)(v);
}

size_t lora_encode_scan(const lora_scan_event_t* ev, uint8_t* out, size_t cap)
{
    if (ev == NULL || out == NULL) {
        return 0u;
    }
    if (ev->uid_len < LORA_UID_MIN || ev->uid_len > LORA_UID_MAX) {
        return 0u;
    }
    if (ev->event_type < LORA_EVT_COMPLETE || ev->event_type > LORA_EVT_QC_FAIL) {
        return 0u;
    }

    size_t len = 10u + ev->uid_len;
    const bool op_valid = ev->op_len > 0u && ev->op_len <= LORA_UID_MAX;
    const bool op_fits  = op_valid && (len + 1u + ev->op_len) <= LORA_PAYLOAD_MAX;
    if (op_fits) {
        len += 1u + ev->op_len;
    }
    if (cap < len) {
        return 0u;
    }

    uint8_t flags = (uint8_t)(ev->flags & (LORA_SCAN_FLAG_QC_STATION | LORA_SCAN_FLAG_REPLAY));
    if (ev->epoch != 0u) {
        flags |= LORA_SCAN_FLAG_TIME_SYNCED;
    }
    if (op_fits) {
        flags |= LORA_SCAN_FLAG_OPERATOR;
    }

    out[0] = LORA_SCHEMA_VERSION;
    out[1] = flags;
    put_u32(&out[2], ev->epoch);
    put_u16(&out[6], ev->seq);
    out[8] = ev->event_type;
    out[9] = ev->uid_len;
    memcpy(&out[10], ev->uid, ev->uid_len);
    if (op_fits) {
        out[10u + ev->uid_len] = ev->op_len;
        memcpy(&out[11u + ev->uid_len], ev->op, ev->op_len);
    }
    return len;
}

size_t lora_encode_heartbeat(const lora_heartbeat_t* hb, uint8_t* out, size_t cap)
{
    if (hb == NULL || out == NULL || cap < LORA_HEARTBEAT_LEN) {
        return 0u;
    }
    uint8_t flags = (uint8_t)(hb->flags & (LORA_HB_FLAG_OPERATOR_LOGGED_IN |
                                           LORA_HB_FLAG_STATION_MAPPED |
                                           LORA_HB_FLAG_WIFI_ASSOCIATED));
    if (hb->epoch != 0u) {
        flags |= LORA_HB_FLAG_TIME_SYNCED;
    }
    out[0] = LORA_SCHEMA_VERSION;
    out[1] = flags;
    put_u32(&out[2], hb->uptime_s);
    put_u16(&out[6], hb->queued_http_events);
    put_u16(&out[8], hb->last_seq);
    out[10] = hb->lora_dropped;
    out[11] = hb->fw[0];
    out[12] = hb->fw[1];
    out[13] = hb->fw[2];
    out[14] = (uint8_t)hb->wifi_rssi;
    out[15] = hb->offline_reason;
    put_u32(&out[16], hb->epoch);
    return LORA_HEARTBEAT_LEN;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool lora_hex_to_bytes(const char* hex, uint8_t* out, uint8_t cap, uint8_t* len)
{
    if (hex == NULL || out == NULL || len == NULL) {
        return false;
    }
    const size_t n = strlen(hex);
    if (n == 0u || (n & 1u) != 0u || n / 2u > cap) {
        return false;
    }
    for (size_t i = 0u; i < n; i += 2u) {
        const int hi = hex_nibble(hex[i]);
        const int lo = hex_nibble(hex[i + 1u]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i / 2u] = (uint8_t)((hi << 4) | lo);
    }
    *len = (uint8_t)(n / 2u);
    return true;
}

bool lora_fw_version_bytes(const char* ver, uint8_t out[3])
{
    if (ver == NULL || out == NULL) {
        return false;
    }
    unsigned part = 0u;
    unsigned value = 0u;
    bool have_digit = false;
    for (const char* c = ver;; c++) {
        if (*c >= '0' && *c <= '9') {
            value = value * 10u + (unsigned)(*c - '0');
            if (value > 255u) {
                return false;
            }
            have_digit = true;
        } else if (*c == '.' || *c == '\0') {
            if (!have_digit || part >= 3u) {
                return false;
            }
            out[part++] = (uint8_t)value;
            value = 0u;
            have_digit = false;
            if (*c == '\0') {
                break;
            }
        } else {
            return false;
        }
    }
    return part == 3u;
}
