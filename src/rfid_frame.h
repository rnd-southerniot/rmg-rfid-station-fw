/*
 * rfid_frame.h — pure frame parser for 7941E-family dual-frequency (125 kHz + 13.56 MHz)
 * UART RFID reader modules.
 *
 * No Arduino dependencies: this file compiles for the ESP32 target AND for the host-native
 * unit tests (test/test_rfid_frame). All time is passed in by the caller in milliseconds.
 *
 * Frame layout (ASSUMED from vendor-family documentation; every field below is adjustable so the
 * Phase-3 bench capture can settle it without touching the state machine):
 *
 *   [0]       STX   0x02
 *   [1]       LEN   total frame length in bytes, STX..ETX inclusive (0x0A for a 4-byte Mifare UID)
 *   [2]       TYPE  0x01 = 13.56 MHz Mifare / ISO14443A UID, 0x02 = 125 kHz EM4100
 *   [3..]     DATA  LEN-5 bytes when an ETX is present, LEN-4 bytes without
 *   [LEN-2]   BCC   XOR of LEN..last DATA byte (default mode)
 *   [LEN-1]   ETX   0x03 (optional; etx = 0 disables the trailer)
 *
 * The reader re-emits the frame periodically while a card stays on the antenna. The parser
 * suppresses those repeats (RFID_EVT_REPEAT) so the application sees one presentation per
 * card, matching the MFRC522 "halted card is not re-detected" semantics main.cpp relies on.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RFID_FRAME_STX        0x02u
#define RFID_FRAME_ETX        0x03u
#define RFID_FRAME_MAX        20u   /* largest frame we buffer (LEN must be <= this) */
#define RFID_UID_MAX          10u   /* ISO14443A triple-size UID */

/* Card type byte values (ASSUMED, confirmed at the bench). */
#define RFID_CARD_MIFARE      0x01u
#define RFID_CARD_EM4100      0x02u

typedef enum {
    RFID_BCC_XOR_LEN_TO_DATA = 0, /* XOR of bytes [1 .. BCC-1]  (vendor default) */
    RFID_BCC_XOR_ALL         = 1, /* XOR of bytes [0 .. BCC-1]  (alternate family variant) */
    RFID_BCC_NONE            = 2  /* no check — discovery only */
} rfid_bcc_mode_t;

typedef enum {
    RFID_EVT_NONE = 0,   /* byte consumed, nothing to report */
    RFID_EVT_NEW_CARD,   /* a valid frame with a UID not seen within hold_gap_ms */
    RFID_EVT_REPEAT,     /* a valid frame repeating the card still on the antenna */
    RFID_EVT_BAD_FRAME   /* framing / length / BCC / ETX violation; parser resynced */
} rfid_event_t;

typedef struct {
    uint8_t card_type;               /* TYPE byte as emitted */
    uint8_t data_len;                /* raw DATA length */
    uint8_t data[RFID_UID_MAX + 1];  /* raw DATA bytes (up to 11: a zero pad + 10-byte UID) */
    uint8_t uid_len;                 /* UID after the pad policy (<= RFID_UID_MAX) */
    uint8_t uid[RFID_UID_MAX];
} rfid_frame_t;

typedef struct {
    /* configuration */
    uint32_t interbyte_timeout_ms;   /* gap that aborts a partial frame */
    uint32_t hold_gap_ms;            /* same UID within this window = repeat, not a new card */
    uint8_t  bcc_mode;               /* rfid_bcc_mode_t */
    uint8_t  etx;                    /* expected trailer byte, 0 = frame has no trailer */
    uint8_t  strip_mifare_pad;       /* 1: TYPE==MIFARE, DATA==5 bytes, DATA[0]==0 -> UID = DATA[1..4] */
    /* frame assembly */
    uint8_t  state;
    uint8_t  buf[RFID_FRAME_MAX];
    uint8_t  idx;
    uint8_t  expect_len;
    uint32_t last_byte_ms;
    /* repeat suppression */
    bool     have_last;
    uint8_t  last_uid_len;
    uint8_t  last_uid[RFID_UID_MAX];
    uint32_t last_seen_ms;
    /* counters (exposed on the bench console) */
    uint32_t frames_ok;
    uint32_t frames_bad;
    uint32_t frames_repeat;
    uint32_t resyncs;
    uint32_t noise_bytes;            /* bytes discarded while waiting for STX */
} rfid_parser_t;

/* Initialise with the vendor-default format (BCC over LEN..DATA, ETX 0x03, Mifare pad stripped). */
void rfid_parser_init(rfid_parser_t* p, uint32_t interbyte_timeout_ms, uint32_t hold_gap_ms);

/* Override the frame format (used by the discovery gate and by the board header). */
void rfid_parser_set_format(rfid_parser_t* p, uint8_t bcc_mode, uint8_t etx, uint8_t strip_mifare_pad);

/* Feed one received byte. Returns an event; *out is filled for NEW_CARD and REPEAT. */
rfid_event_t rfid_parser_feed(rfid_parser_t* p, uint8_t b, uint32_t now_ms, rfid_frame_t* out);

/* Call when idle so a stalled partial frame is dropped after interbyte_timeout_ms. */
void rfid_parser_tick(rfid_parser_t* p, uint32_t now_ms);

/* Forget the last card so the next frame (even the same UID) is reported as NEW_CARD. */
void rfid_parser_forget_last(rfid_parser_t* p);

/*
 * Format a UID as uppercase hex, two digits per byte, no separators — byte for byte the same
 * string rfid_mfrc522.cpp produces, so backend enrolments carry over. `reverse` emits the bytes
 * last-to-first (for readers that stream the UID little-endian). Returns the string length,
 * 0 if `cap` is too small (needs 2*len+1).
 */
size_t rfid_uid_to_hex(const uint8_t* uid, uint8_t len, bool reverse, char* out, size_t cap);

#ifdef __cplusplus
}
#endif
