/*
 * Host-native Unity tests for src/rfid_frame.cpp (run with `pio test -e native`).
 *
 * Vectors marked ASSUMED encode the vendor-family frame layout; Phase 3 replaces/extends them
 * with bytes captured from the real module (`rfid raw on` on the bench console).
 */
#include <unity.h>
#include <string.h>
#include "rfid_frame.h"

/* ASSUMED: 4-byte Mifare UID A1B2C3D4 with a leading zero pad. BCC = XOR(0A,01,00,A1,B2,C3,D4) = 0F. */
static const uint8_t MIFARE_FRAME[10] = { 0x02, 0x0A, 0x01, 0x00, 0xA1, 0xB2, 0xC3, 0xD4, 0x0F, 0x03 };
/* ASSUMED: 5-byte EM4100 ID 123456789A. BCC = XOR(0A,02,12,34,56,78,9A) = 9A. */
static const uint8_t EM4100_FRAME[10] = { 0x02, 0x0A, 0x02, 0x12, 0x34, 0x56, 0x78, 0x9A, 0x9A, 0x03 };

/* PROVEN 2026-09-24, captured from the module on the RAK3212 station at 115200 8N1:
 * STX "4050B047" CR LF ETX. */
static const uint8_t ASCII_FRAME[12] = { 0x02, '4', '0', '5', '0', 'B', '0', '4', '7', 0x0D, 0x0A, 0x03 };

static rfid_parser_t p;
static rfid_frame_t f;

void setUp(void)
{
    rfid_parser_init(&p, 50u, 800u);
    memset(&f, 0, sizeof(f));
}

void tearDown(void) {}

static rfid_event_t feed_all(const uint8_t* bytes, size_t n, uint32_t t0)
{
    rfid_event_t last = RFID_EVT_NONE;
    for (size_t i = 0; i < n; i++) {
        last = rfid_parser_feed(&p, bytes[i], t0 + (uint32_t)i, &f);
    }
    return last;
}

static void test_mifare_frame_is_new_card_with_pad_stripped(void)
{
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(MIFARE_FRAME, sizeof(MIFARE_FRAME), 1000u));
    TEST_ASSERT_EQUAL_HEX8(RFID_CARD_MIFARE, f.card_type);
    TEST_ASSERT_EQUAL_UINT8(5u, f.data_len);
    TEST_ASSERT_EQUAL_UINT8(4u, f.uid_len);
    const uint8_t expect[4] = { 0xA1, 0xB2, 0xC3, 0xD4 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, f.uid, 4);
    TEST_ASSERT_EQUAL_UINT32(1u, p.frames_ok);
    TEST_ASSERT_EQUAL_UINT32(0u, p.frames_bad);
}

static void test_em4100_frame_keeps_five_bytes(void)
{
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(EM4100_FRAME, sizeof(EM4100_FRAME), 1000u));
    TEST_ASSERT_EQUAL_HEX8(RFID_CARD_EM4100, f.card_type);
    TEST_ASSERT_EQUAL_UINT8(5u, f.uid_len);
    const uint8_t expect[5] = { 0x12, 0x34, 0x56, 0x78, 0x9A };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, f.uid, 5);
}

static void test_uid_hex_matches_mfrc522_format(void)
{
    char s[2 * RFID_UID_MAX + 1];
    const uint8_t uid[4] = { 0xA1, 0x0B, 0xC3, 0xD4 };
    TEST_ASSERT_EQUAL_size_t(8u, rfid_uid_to_hex(uid, 4, false, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("A10BC3D4", s);
    TEST_ASSERT_EQUAL_size_t(8u, rfid_uid_to_hex(uid, 4, true, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("D4C30BA1", s);
    /* too-small buffer → 0 and empty string */
    char tiny[4];
    TEST_ASSERT_EQUAL_size_t(0u, rfid_uid_to_hex(uid, 4, false, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("", tiny);
}

static void test_bad_bcc_is_rejected_and_parser_resyncs(void)
{
    uint8_t bad[10];
    memcpy(bad, MIFARE_FRAME, sizeof(bad));
    bad[8] ^= 0x01;
    TEST_ASSERT_EQUAL(RFID_EVT_BAD_FRAME, feed_all(bad, sizeof(bad), 1000u));
    TEST_ASSERT_EQUAL_UINT32(1u, p.frames_bad);
    /* next good frame parses normally */
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(MIFARE_FRAME, sizeof(MIFARE_FRAME), 2000u));
}

static void test_bad_etx_is_rejected(void)
{
    uint8_t bad[10];
    memcpy(bad, MIFARE_FRAME, sizeof(bad));
    bad[9] = 0x00;
    TEST_ASSERT_EQUAL(RFID_EVT_BAD_FRAME, feed_all(bad, sizeof(bad), 1000u));
}

static void test_len_out_of_range_is_rejected_early(void)
{
    const uint8_t bad_short[2] = { 0x02, 0x04 };
    TEST_ASSERT_EQUAL(RFID_EVT_BAD_FRAME, feed_all(bad_short, 2, 1000u));
    const uint8_t bad_long[2] = { 0x02, 0x40 };
    TEST_ASSERT_EQUAL(RFID_EVT_BAD_FRAME, feed_all(bad_long, 2, 2000u));
    TEST_ASSERT_EQUAL_UINT32(2u, p.frames_bad);
}

static void test_noise_before_stx_is_ignored(void)
{
    const uint8_t noise[3] = { 0xFF, 0x00, 0x55 };
    TEST_ASSERT_EQUAL(RFID_EVT_NONE, feed_all(noise, 3, 900u));
    TEST_ASSERT_EQUAL_UINT32(3u, p.noise_bytes);
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(MIFARE_FRAME, sizeof(MIFARE_FRAME), 1000u));
}

static void test_repeat_within_hold_gap_then_new_after_gap(void)
{
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(MIFARE_FRAME, sizeof(MIFARE_FRAME), 1000u));
    TEST_ASSERT_EQUAL(RFID_EVT_REPEAT,   feed_all(MIFARE_FRAME, sizeof(MIFARE_FRAME), 1300u));
    TEST_ASSERT_EQUAL(RFID_EVT_REPEAT,   feed_all(MIFARE_FRAME, sizeof(MIFARE_FRAME), 1900u)); /* window slides */
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(MIFARE_FRAME, sizeof(MIFARE_FRAME), 3000u)); /* >800 ms idle */
    TEST_ASSERT_EQUAL_UINT32(2u, p.frames_repeat);
    TEST_ASSERT_EQUAL_UINT32(4u, p.frames_ok);
}

static void test_different_card_within_gap_is_new(void)
{
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(MIFARE_FRAME, sizeof(MIFARE_FRAME), 1000u));
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(EM4100_FRAME, sizeof(EM4100_FRAME), 1200u));
}

static void test_forget_last_reports_same_card_as_new(void)
{
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(MIFARE_FRAME, sizeof(MIFARE_FRAME), 1000u));
    rfid_parser_forget_last(&p);
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(MIFARE_FRAME, sizeof(MIFARE_FRAME), 1100u));
}

static void test_interbyte_timeout_drops_partial_frame(void)
{
    feed_all(MIFARE_FRAME, 5, 1000u);                 /* half a frame ... */
    rfid_parser_tick(&p, 1000u + 5u + 100u);          /* ... then silence beyond 50 ms */
    TEST_ASSERT_EQUAL_UINT32(1u, p.resyncs);
    /* the remaining bytes of the old frame are noise now; a fresh frame still parses */
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(MIFARE_FRAME, sizeof(MIFARE_FRAME), 2000u));
}

static void test_interbyte_timeout_inside_feed(void)
{
    feed_all(MIFARE_FRAME, 5, 1000u);
    /* byte arrives 200 ms late: parser resyncs and treats it as noise/STX search */
    TEST_ASSERT_EQUAL(RFID_EVT_NONE, rfid_parser_feed(&p, MIFARE_FRAME[5], 1300u, &f));
    TEST_ASSERT_EQUAL_UINT32(1u, p.resyncs);
}

static void test_no_etx_variant(void)
{
    rfid_parser_set_format(&p, RFID_BCC_XOR_LEN_TO_DATA, 0u, 1u);
    /* 02 09 01 00 A1 B2 C3 D4 BCC — BCC = XOR(09,01,00,A1,B2,C3,D4) = 0C */
    const uint8_t frame[9] = { 0x02, 0x09, 0x01, 0x00, 0xA1, 0xB2, 0xC3, 0xD4, 0x0C };
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(frame, sizeof(frame), 1000u));
    TEST_ASSERT_EQUAL_UINT8(4u, f.uid_len);
    TEST_ASSERT_EQUAL_HEX8(0xA1, f.uid[0]);
}

static void test_bcc_xor_all_variant(void)
{
    rfid_parser_set_format(&p, RFID_BCC_XOR_ALL, RFID_FRAME_ETX, 1u);
    uint8_t frame[10];
    memcpy(frame, MIFARE_FRAME, sizeof(frame));
    frame[8] = 0x0F ^ 0x02; /* now covers STX too */
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(frame, sizeof(frame), 1000u));
}

static void test_bcc_none_accepts_anything(void)
{
    rfid_parser_set_format(&p, RFID_BCC_NONE, RFID_FRAME_ETX, 1u);
    uint8_t frame[10];
    memcpy(frame, MIFARE_FRAME, sizeof(frame));
    frame[8] = 0x00;
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(frame, sizeof(frame), 1000u));
}

static void test_pad_strip_disabled_keeps_five_bytes(void)
{
    rfid_parser_set_format(&p, RFID_BCC_XOR_LEN_TO_DATA, RFID_FRAME_ETX, 0u);
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(MIFARE_FRAME, sizeof(MIFARE_FRAME), 1000u));
    TEST_ASSERT_EQUAL_UINT8(5u, f.uid_len);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.uid[0]);
}

static void test_seven_byte_uid_frame(void)
{
    /* 02 0C 01 04 8F 2A 11 22 33 44 BCC 03 : 7-byte UID, no pad. BCC = XOR(0C,01,04,8F,2A,11,22,33,44) */
    uint8_t frame[12] = { 0x02, 0x0C, 0x01, 0x04, 0x8F, 0x2A, 0x11, 0x22, 0x33, 0x44, 0x00, 0x03 };
    uint8_t bcc = 0;
    for (int i = 1; i <= 9; i++) bcc ^= frame[i];
    frame[10] = bcc;
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(frame, sizeof(frame), 1000u));
    TEST_ASSERT_EQUAL_UINT8(7u, f.uid_len);
    TEST_ASSERT_EQUAL_HEX8(0x04, f.uid[0]);
    TEST_ASSERT_EQUAL_HEX8(0x44, f.uid[6]);
}

static void ascii_setup(void)
{
    rfid_parser_set_frame_format(&p, RFID_FORMAT_ASCII_HEX);
}

static void test_ascii_real_frame_4050B047(void)
{
    ascii_setup();
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(ASCII_FRAME, sizeof(ASCII_FRAME), 1000u));
    TEST_ASSERT_EQUAL_HEX8(RFID_CARD_UNKNOWN, f.card_type);
    TEST_ASSERT_EQUAL_UINT8(4u, f.uid_len);
    const uint8_t expect[4] = { 0x40, 0x50, 0xB0, 0x47 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, f.uid, 4);
    char s[2 * RFID_UID_MAX + 1];
    rfid_uid_to_hex(f.uid, f.uid_len, false, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("4050B047", s);
    TEST_ASSERT_EQUAL_UINT32(1u, p.frames_ok);
    TEST_ASSERT_EQUAL_UINT32(0u, p.frames_bad);
}

static void test_ascii_lowercase_and_ten_digits(void)
{
    ascii_setup();
    const uint8_t frame[14] = { 0x02, '0', 'a', '1', 'b', '2', 'c', '3', 'd', '4', 'e', 0x0D, 0x0A, 0x03 };
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(frame, sizeof(frame), 1000u));
    TEST_ASSERT_EQUAL_UINT8(5u, f.uid_len);
    const uint8_t expect[5] = { 0x0A, 0x1B, 0x2C, 0x3D, 0x4E };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, f.uid, 5);
}

static void test_ascii_odd_digit_count_is_bad(void)
{
    ascii_setup();
    const uint8_t frame[7] = { 0x02, '4', '0', '5', 0x0D, 0x0A, 0x03 };
    TEST_ASSERT_EQUAL(RFID_EVT_BAD_FRAME, feed_all(frame, sizeof(frame), 1000u));
    TEST_ASSERT_EQUAL_UINT32(1u, p.frames_bad);
}

static void test_ascii_non_hex_is_bad_then_recovers(void)
{
    ascii_setup();
    const uint8_t bad[5] = { 0x02, '4', 'G', '5', '0' };
    TEST_ASSERT_EQUAL(RFID_EVT_BAD_FRAME, feed_all(bad, 3, 1000u));   /* 'G' rejects immediately */
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(ASCII_FRAME, sizeof(ASCII_FRAME), 2000u));
}

static void test_ascii_missing_lf_or_etx_is_bad(void)
{
    ascii_setup();
    const uint8_t no_lf[11] = { 0x02, '4', '0', '5', '0', 'B', '0', '4', '7', 0x0D, 0x03 };
    TEST_ASSERT_EQUAL(RFID_EVT_BAD_FRAME, feed_all(no_lf, sizeof(no_lf), 1000u));
    const uint8_t no_etx[12] = { 0x02, '4', '0', '5', '0', 'B', '0', '4', '7', 0x0D, 0x0A, 0x00 };
    TEST_ASSERT_EQUAL(RFID_EVT_BAD_FRAME, feed_all(no_etx, sizeof(no_etx), 2000u));
    TEST_ASSERT_EQUAL_UINT32(2u, p.frames_bad);
}

static void test_ascii_without_etx_when_configured(void)
{
    ascii_setup();
    rfid_parser_set_format(&p, RFID_BCC_NONE, 0u, 0u);   /* etx = 0: frame ends at LF */
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(ASCII_FRAME, 11, 1000u));
    TEST_ASSERT_EQUAL_UINT8(4u, f.uid_len);
}

static void test_ascii_repeat_suppression(void)
{
    ascii_setup();
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(ASCII_FRAME, sizeof(ASCII_FRAME), 1000u));
    TEST_ASSERT_EQUAL(RFID_EVT_REPEAT,   feed_all(ASCII_FRAME, sizeof(ASCII_FRAME), 1500u));
    TEST_ASSERT_EQUAL(RFID_EVT_NEW_CARD, feed_all(ASCII_FRAME, sizeof(ASCII_FRAME), 3000u));
}

static void test_ascii_too_many_digits_is_bad(void)
{
    ascii_setup();
    uint8_t frame[1 + 22];
    frame[0] = 0x02;
    for (int i = 1; i <= 22; i++) frame[i] = 'A';   /* 22 digits > 20 (RFID_UID_MAX bytes) */
    /* the 21st digit rejects the frame; the 22nd byte is then noise while waiting for STX */
    TEST_ASSERT_EQUAL(RFID_EVT_NONE, feed_all(frame, sizeof(frame), 1000u));
    TEST_ASSERT_EQUAL_UINT32(1u, p.frames_bad);
    TEST_ASSERT_EQUAL_UINT32(1u, p.noise_bytes);
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_mifare_frame_is_new_card_with_pad_stripped);
    RUN_TEST(test_em4100_frame_keeps_five_bytes);
    RUN_TEST(test_uid_hex_matches_mfrc522_format);
    RUN_TEST(test_bad_bcc_is_rejected_and_parser_resyncs);
    RUN_TEST(test_bad_etx_is_rejected);
    RUN_TEST(test_len_out_of_range_is_rejected_early);
    RUN_TEST(test_noise_before_stx_is_ignored);
    RUN_TEST(test_repeat_within_hold_gap_then_new_after_gap);
    RUN_TEST(test_different_card_within_gap_is_new);
    RUN_TEST(test_forget_last_reports_same_card_as_new);
    RUN_TEST(test_interbyte_timeout_drops_partial_frame);
    RUN_TEST(test_interbyte_timeout_inside_feed);
    RUN_TEST(test_no_etx_variant);
    RUN_TEST(test_bcc_xor_all_variant);
    RUN_TEST(test_bcc_none_accepts_anything);
    RUN_TEST(test_pad_strip_disabled_keeps_five_bytes);
    RUN_TEST(test_seven_byte_uid_frame);
    RUN_TEST(test_ascii_real_frame_4050B047);
    RUN_TEST(test_ascii_lowercase_and_ten_digits);
    RUN_TEST(test_ascii_odd_digit_count_is_bad);
    RUN_TEST(test_ascii_non_hex_is_bad_then_recovers);
    RUN_TEST(test_ascii_missing_lf_or_etx_is_bad);
    RUN_TEST(test_ascii_without_etx_when_configured);
    RUN_TEST(test_ascii_repeat_suppression);
    RUN_TEST(test_ascii_too_many_digits_is_bad);
    return UNITY_END();
}
