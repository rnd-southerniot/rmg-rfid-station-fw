/*
 * Host-native Unity tests for src/lora_payload.cpp (run with `pio test -e native`).
 *
 * The golden vectors here are the SAME ones tools/chirpstack/codec_test.js decodes, so the
 * firmware encoder and the ChirpStack decoder are cross-checked without hardware.
 * Keep docs/LORAWAN_PAYLOAD.md in sync when changing them.
 */
#include <unity.h>
#include <string.h>
#include "lora_payload.h"

static uint8_t buf[LORA_PAYLOAD_MAX + 8];

void setUp(void) { memset(buf, 0xEE, sizeof(buf)); }
void tearDown(void) {}

static const uint32_t EPOCH = 1790000000u; /* 0x6AB13B80 */

static lora_scan_event_t base_scan(void)
{
    lora_scan_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.epoch = EPOCH;
    ev.seq = 1234u; /* 0x04D2 */
    ev.event_type = LORA_EVT_COMPLETE;
    ev.uid_len = 4u;
    const uint8_t uid[4] = { 0xA1, 0xB2, 0xC3, 0xD4 };
    memcpy(ev.uid, uid, 4);
    return ev;
}

/* Vector S1 */
static void test_scan_complete_no_operator(void)
{
    lora_scan_event_t ev = base_scan();
    const uint8_t expect[14] = { 0x01, 0x01, 0x6A, 0xB1, 0x3B, 0x80, 0x04, 0xD2, 0x01, 0x04,
                                 0xA1, 0xB2, 0xC3, 0xD4 };
    TEST_ASSERT_EQUAL_size_t(14u, lora_encode_scan(&ev, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, 14);
}

/* Vector S2 */
static void test_scan_qc_pass_with_operator(void)
{
    lora_scan_event_t ev = base_scan();
    ev.event_type = LORA_EVT_QC_PASS;
    ev.flags = LORA_SCAN_FLAG_QC_STATION;
    ev.op_len = 4u;
    const uint8_t op[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    memcpy(ev.op, op, 4);
    const uint8_t expect[19] = { 0x01, 0x07, 0x6A, 0xB1, 0x3B, 0x80, 0x04, 0xD2, 0x02, 0x04,
                                 0xA1, 0xB2, 0xC3, 0xD4, 0x04, 0xDE, 0xAD, 0xBE, 0xEF };
    TEST_ASSERT_EQUAL_size_t(19u, lora_encode_scan(&ev, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, 19);
}

/* Vector S3: unsynced clock, 10-byte UID, operator trailer does not fit → omitted, flag clear */
static void test_scan_unsynced_long_uid_drops_operator(void)
{
    lora_scan_event_t ev = base_scan();
    ev.epoch = 0u;
    ev.event_type = LORA_EVT_QC_FAIL;
    ev.flags = LORA_SCAN_FLAG_REPLAY;
    ev.uid_len = 10u;
    for (uint8_t i = 0; i < 10; i++) ev.uid[i] = (uint8_t)(0x10 + i);
    ev.op_len = 10u;
    for (uint8_t i = 0; i < 10; i++) ev.op[i] = (uint8_t)(0xA0 + i);
    const uint8_t expect[20] = { 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x04, 0xD2, 0x03, 0x0A,
                                 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19 };
    TEST_ASSERT_EQUAL_size_t(20u, lora_encode_scan(&ev, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, 20);
}

/* Vector S4: 7-byte UID + 7-byte operator = exactly 24 bytes → fits */
static void test_scan_exactly_24_bytes_keeps_operator(void)
{
    lora_scan_event_t ev = base_scan();
    ev.uid_len = 7u;
    ev.op_len = 6u; /* 10 + 7 + 1 + 6 = 24 */
    TEST_ASSERT_EQUAL_size_t(24u, lora_encode_scan(&ev, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0x01 | 0x04, buf[1]);
    ev.op_len = 7u; /* 25 → operator dropped */
    TEST_ASSERT_EQUAL_size_t(17u, lora_encode_scan(&ev, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);
}

static void test_scan_rejects_bad_uid_len_and_type(void)
{
    lora_scan_event_t ev = base_scan();
    ev.uid_len = 3u;
    TEST_ASSERT_EQUAL_size_t(0u, lora_encode_scan(&ev, buf, sizeof(buf)));
    ev = base_scan();
    ev.uid_len = 11u;
    TEST_ASSERT_EQUAL_size_t(0u, lora_encode_scan(&ev, buf, sizeof(buf)));
    ev = base_scan();
    ev.event_type = 0u;
    TEST_ASSERT_EQUAL_size_t(0u, lora_encode_scan(&ev, buf, sizeof(buf)));
    ev = base_scan();
    ev.event_type = 4u;
    TEST_ASSERT_EQUAL_size_t(0u, lora_encode_scan(&ev, buf, sizeof(buf)));
}

static void test_scan_rejects_small_buffer(void)
{
    lora_scan_event_t ev = base_scan();
    TEST_ASSERT_EQUAL_size_t(0u, lora_encode_scan(&ev, buf, 13u));
    TEST_ASSERT_EQUAL_size_t(14u, lora_encode_scan(&ev, buf, 14u));
}

static void test_scan_ignores_derived_flags_from_caller(void)
{
    lora_scan_event_t ev = base_scan();
    ev.flags = 0xFF; /* caller may not set TIME_SYNCED/OPERATOR/reserved bits */
    ev.epoch = 0u;
    TEST_ASSERT_EQUAL_size_t(14u, lora_encode_scan(&ev, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(LORA_SCAN_FLAG_QC_STATION | LORA_SCAN_FLAG_REPLAY, buf[1]);
}

/* Vector H1 */
static void test_heartbeat_golden(void)
{
    lora_heartbeat_t hb;
    memset(&hb, 0, sizeof(hb));
    hb.uptime_s = 3600u;
    hb.queued_http_events = 5u;
    hb.last_seq = 1234u;
    hb.lora_dropped = 2u;
    hb.fw[0] = 0; hb.fw[1] = 2; hb.fw[2] = 0;
    hb.wifi_rssi = -67;
    hb.offline_reason = LORA_OFFLINE_WIFI_DOWN;
    hb.epoch = EPOCH;
    hb.flags = LORA_HB_FLAG_OPERATOR_LOGGED_IN | LORA_HB_FLAG_STATION_MAPPED;
    const uint8_t expect[20] = { 0x01, 0x07, 0x00, 0x00, 0x0E, 0x10, 0x00, 0x05, 0x04, 0xD2,
                                 0x02, 0x00, 0x02, 0x00, 0xBD, 0x01, 0x6A, 0xB1, 0x3B, 0x80 };
    TEST_ASSERT_EQUAL_size_t(20u, lora_encode_heartbeat(&hb, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, 20);
}

static void test_heartbeat_unsynced_and_small_buffer(void)
{
    lora_heartbeat_t hb;
    memset(&hb, 0, sizeof(hb));
    hb.flags = 0xFF;
    TEST_ASSERT_EQUAL_size_t(20u, lora_encode_heartbeat(&hb, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0x0E, buf[1]); /* TIME_SYNCED cleared, reserved bits masked */
    TEST_ASSERT_EQUAL_size_t(0u, lora_encode_heartbeat(&hb, buf, 19u));
}

static void test_hex_to_bytes(void)
{
    uint8_t out[LORA_UID_MAX];
    uint8_t len = 0;
    TEST_ASSERT_TRUE(lora_hex_to_bytes("A1B2C3D4", out, sizeof(out), &len));
    TEST_ASSERT_EQUAL_UINT8(4u, len);
    const uint8_t expect[4] = { 0xA1, 0xB2, 0xC3, 0xD4 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 4);
    TEST_ASSERT_TRUE(lora_hex_to_bytes("a1b2c3d4", out, sizeof(out), &len));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 4);
    TEST_ASSERT_FALSE(lora_hex_to_bytes("A1B2C3D", out, sizeof(out), &len));   /* odd */
    TEST_ASSERT_FALSE(lora_hex_to_bytes("", out, sizeof(out), &len));          /* empty */
    TEST_ASSERT_FALSE(lora_hex_to_bytes("A1:B2", out, sizeof(out), &len));     /* separator */
    TEST_ASSERT_FALSE(lora_hex_to_bytes("0102030405060708090A0B", out, sizeof(out), &len)); /* 11 > cap */
    TEST_ASSERT_TRUE(lora_hex_to_bytes("0102030405060708090A", out, sizeof(out), &len));
    TEST_ASSERT_EQUAL_UINT8(10u, len);
}

static void test_fw_version_bytes(void)
{
    uint8_t v[3];
    TEST_ASSERT_TRUE(lora_fw_version_bytes("0.2.0", v));
    TEST_ASSERT_EQUAL_UINT8(0, v[0]); TEST_ASSERT_EQUAL_UINT8(2, v[1]); TEST_ASSERT_EQUAL_UINT8(0, v[2]);
    TEST_ASSERT_TRUE(lora_fw_version_bytes("12.34.255", v));
    TEST_ASSERT_EQUAL_UINT8(255, v[2]);
    TEST_ASSERT_FALSE(lora_fw_version_bytes("1.2", v));
    TEST_ASSERT_FALSE(lora_fw_version_bytes("1.2.3.4", v));
    TEST_ASSERT_FALSE(lora_fw_version_bytes("1.2.256", v));
    TEST_ASSERT_FALSE(lora_fw_version_bytes("1..2", v));
    TEST_ASSERT_FALSE(lora_fw_version_bytes("v1.2.3", v));
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_scan_complete_no_operator);
    RUN_TEST(test_scan_qc_pass_with_operator);
    RUN_TEST(test_scan_unsynced_long_uid_drops_operator);
    RUN_TEST(test_scan_exactly_24_bytes_keeps_operator);
    RUN_TEST(test_scan_rejects_bad_uid_len_and_type);
    RUN_TEST(test_scan_rejects_small_buffer);
    RUN_TEST(test_scan_ignores_derived_flags_from_caller);
    RUN_TEST(test_heartbeat_golden);
    RUN_TEST(test_heartbeat_unsynced_and_small_buffer);
    RUN_TEST(test_hex_to_bytes);
    RUN_TEST(test_fw_version_bytes);
    return UNITY_END();
}
