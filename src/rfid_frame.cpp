/*
 * rfid_frame.cpp — see rfid_frame.h. Pure C-style code; no Arduino, no heap, no I/O.
 */
#include "rfid_frame.h"

#include <string.h>

enum { ST_WAIT_STX = 0, ST_IN_FRAME = 1 };

static uint8_t overhead_bytes(const rfid_parser_t* p)
{
    /* STX + LEN + TYPE + BCC (+ ETX) */
    return (uint8_t)(p->etx ? 5u : 4u);
}

void rfid_parser_init(rfid_parser_t* p, uint32_t interbyte_timeout_ms, uint32_t hold_gap_ms)
{
    memset(p, 0, sizeof(*p));
    p->interbyte_timeout_ms = interbyte_timeout_ms;
    p->hold_gap_ms          = hold_gap_ms;
    p->bcc_mode             = RFID_BCC_XOR_LEN_TO_DATA;
    p->etx                  = RFID_FRAME_ETX;
    p->strip_mifare_pad     = 1u;
    p->state                = ST_WAIT_STX;
}

void rfid_parser_set_format(rfid_parser_t* p, uint8_t bcc_mode, uint8_t etx, uint8_t strip_mifare_pad)
{
    p->bcc_mode         = bcc_mode;
    p->etx              = etx;
    p->strip_mifare_pad = strip_mifare_pad ? 1u : 0u;
    p->state            = ST_WAIT_STX;
    p->idx              = 0u;
}

void rfid_parser_forget_last(rfid_parser_t* p)
{
    p->have_last    = false;
    p->last_uid_len = 0u;
}

static void resync(rfid_parser_t* p)
{
    p->state = ST_WAIT_STX;
    p->idx   = 0u;
}

void rfid_parser_tick(rfid_parser_t* p, uint32_t now_ms)
{
    if (p->state == ST_IN_FRAME && (uint32_t)(now_ms - p->last_byte_ms) > p->interbyte_timeout_ms) {
        p->resyncs++;
        resync(p);
    }
}

static bool bcc_ok(const rfid_parser_t* p, uint8_t bcc_pos)
{
    if (p->bcc_mode == RFID_BCC_NONE) {
        return true;
    }
    uint8_t x = 0u;
    uint8_t from = (p->bcc_mode == RFID_BCC_XOR_ALL) ? 0u : 1u;
    for (uint8_t i = from; i < bcc_pos; i++) {
        x ^= p->buf[i];
    }
    return x == p->buf[bcc_pos];
}

static void fill_frame(const rfid_parser_t* p, rfid_frame_t* out)
{
    const uint8_t data_len = (uint8_t)(p->expect_len - overhead_bytes(p));
    out->card_type = p->buf[2];
    out->data_len  = data_len;
    memcpy(out->data, &p->buf[3], data_len);

    const uint8_t* uid = out->data;
    uint8_t uid_len    = data_len;
    if (p->strip_mifare_pad && out->card_type == RFID_CARD_MIFARE && data_len == 5u && out->data[0] == 0u) {
        uid     = &out->data[1];
        uid_len = 4u;
    }
    if (uid_len > RFID_UID_MAX) {
        uid_len = RFID_UID_MAX;
    }
    out->uid_len = uid_len;
    memcpy(out->uid, uid, uid_len);
}

static rfid_event_t classify(rfid_parser_t* p, const rfid_frame_t* f, uint32_t now_ms)
{
    const bool same = p->have_last && p->last_uid_len == f->uid_len &&
                      memcmp(p->last_uid, f->uid, f->uid_len) == 0;
    if (same && (uint32_t)(now_ms - p->last_seen_ms) < p->hold_gap_ms) {
        p->last_seen_ms = now_ms;
        p->frames_repeat++;
        return RFID_EVT_REPEAT;
    }
    p->have_last    = true;
    p->last_uid_len = f->uid_len;
    memcpy(p->last_uid, f->uid, f->uid_len);
    p->last_seen_ms = now_ms;
    return RFID_EVT_NEW_CARD;
}

rfid_event_t rfid_parser_feed(rfid_parser_t* p, uint8_t b, uint32_t now_ms, rfid_frame_t* out)
{
    if (p->state == ST_IN_FRAME && (uint32_t)(now_ms - p->last_byte_ms) > p->interbyte_timeout_ms) {
        /* Stalled partial frame: drop it and treat this byte as a fresh start. */
        p->resyncs++;
        resync(p);
    }

    if (p->state == ST_WAIT_STX) {
        if (b != RFID_FRAME_STX) {
            p->noise_bytes++;
            return RFID_EVT_NONE;
        }
        p->buf[0]       = b;
        p->idx          = 1u;
        p->expect_len   = 0u;
        p->state        = ST_IN_FRAME;
        p->last_byte_ms = now_ms;
        return RFID_EVT_NONE;
    }

    /* ST_IN_FRAME */
    p->last_byte_ms = now_ms;
    p->buf[p->idx++] = b;

    if (p->idx == 2u) {
        /* LEN byte: DATA must be 1..RFID_UID_MAX+1 bytes and the whole frame must fit our buffer. */
        const uint8_t ovh = overhead_bytes(p);
        if (b < (uint8_t)(ovh + 1u) || b > (uint8_t)(ovh + RFID_UID_MAX + 1u) || b > RFID_FRAME_MAX) {
            p->frames_bad++;
            resync(p);
            return RFID_EVT_BAD_FRAME;
        }
        p->expect_len = b;
        return RFID_EVT_NONE;
    }

    if (p->idx < p->expect_len) {
        return RFID_EVT_NONE;
    }

    /* Frame complete. */
    const uint8_t bcc_pos = (uint8_t)(p->expect_len - (p->etx ? 2u : 1u));
    const bool etx_good   = (p->etx == 0u) || (p->buf[p->expect_len - 1u] == p->etx);
    if (!etx_good || !bcc_ok(p, bcc_pos)) {
        p->frames_bad++;
        resync(p);
        return RFID_EVT_BAD_FRAME;
    }

    rfid_frame_t local;
    rfid_frame_t* f = out ? out : &local;
    fill_frame(p, f);
    p->frames_ok++;
    resync(p);
    return classify(p, f, now_ms);
}

size_t rfid_uid_to_hex(const uint8_t* uid, uint8_t len, bool reverse, char* out, size_t cap)
{
    static const char digits[] = "0123456789ABCDEF";
    if (out == NULL || cap < (size_t)len * 2u + 1u) {
        if (out != NULL && cap > 0u) {
            out[0] = '\0';
        }
        return 0u;
    }
    size_t n = 0u;
    for (uint8_t i = 0u; i < len; i++) {
        const uint8_t v = reverse ? uid[len - 1u - i] : uid[i];
        out[n++] = digits[v >> 4];
        out[n++] = digits[v & 0x0Fu];
    }
    out[n] = '\0';
    return n;
}
