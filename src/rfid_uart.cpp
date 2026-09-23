/*
 * rfid_uart.cpp — rfid_reader.h backend for 7941E-family dual-frequency UART readers
 * (RAK3212 build). The reader emits one framed card ID per read on its TX line; nothing is
 * ever sent to it. Frame parsing lives in the pure, host-tested rfid_frame.cpp.
 */
#include "rfid_reader.h"
#include "rfid_uart.h"
#include "config.h"
#include "log.h"
#include "rfid_frame.h"
#include <HardwareSerial.h>

static HardwareSerial rfidSerial(RFID_UART_NUM);   // receive-only: RFID_UART_TX is -1
static rfid_parser_t  parser;
static String         pendingUid;      // latched UID string, consumed by rfidReadUid()
static uint8_t        presence = 0;    // see RfidUartStats::presence
static uint8_t        lastCardType = 0;
static bool           rawDump = false;
static uint32_t       currentBaud = RFID_UART_BAUD;
static uint32_t       lastRawMs = 0;
static char           rawLine[3 * 32 + 24];   // one frame's bytes as hex
static size_t         rawLen = 0;
static uint32_t       rawFirstGapMs = 0;
static uint8_t        rawCount = 0;

static void rawFlush() {
    if (rawCount == 0u) return;
    rawLine[rawLen] = '\0';
    Serial.printf("[RFID raw] +%lums %u bytes:%s\n", (unsigned long)rawFirstGapMs, rawCount, rawLine);
    rawLen = 0u;
    rawCount = 0u;
}

static void rawByte(uint8_t b, uint32_t now) {
    const uint32_t gap = now - lastRawMs;
    if (rawCount > 0u && (gap > 20u || rawCount >= 32u)) {
        rawFlush();
    }
    if (rawCount == 0u) {
        rawFirstGapMs = gap;
    }
    rawLen += (size_t)snprintf(&rawLine[rawLen], sizeof(rawLine) - rawLen, " %02X", b);
    rawCount++;
    lastRawMs = now;
}

void rfidInit() {
    /*
     * Presence probe for a passive module: a powered reader drives its TX line idle-HIGH
     * (UART mark). Meaningful only for a direct or resistor-divider connection — a level
     * shifter with its own pull-up idles HIGH with nothing behind it. bit1 ("frame seen")
     * is the real proof and is only set after a card has been read.
     */
    pinMode(RFID_UART_RX, INPUT_PULLDOWN);
    delay(5);
    presence = digitalRead(RFID_UART_RX) ? 0x01u : 0x00u;

    rfidSerial.setRxBufferSize(256);   // must precede begin() on Arduino core 2.x
    rfidSerial.begin(RFID_UART_BAUD, SERIAL_8N1, RFID_UART_RX, RFID_UART_TX);

    rfid_parser_init(&parser, RFID_UART_INTERBYTE_MS, RFID_UART_HOLD_GAP_MS);
    rfid_parser_set_format(&parser, RFID_UART_BCC_MODE, RFID_UART_ETX, RFID_UART_STRIP_MIFARE_PAD);
    rfid_parser_set_frame_format(&parser, RFID_UART_FORMAT);

    LOG_I("[RFID] UART%d reader on RX=%d @%d 8N1 (%s frames), idle line %s\n",
          RFID_UART_NUM, RFID_UART_RX, RFID_UART_BAUD,
          RFID_UART_FORMAT == RFID_FORMAT_ASCII_HEX ? "ASCII" : "binary",
          (presence & 0x01u) ? "HIGH (reader present)" : "LOW (no reader?)");
}

bool rfidCardPresent() {
    if (!pendingUid.isEmpty()) {
        return true;   // latched and not yet consumed
    }
    if (rawDump) {
        return false;  // bench raw dump owns the reader (see rfidBenchService)
    }

    const uint32_t now = millis();
    int budget = 64;   // bound the work per loop pass
    while (budget-- > 0 && rfidSerial.available() > 0) {
        const uint8_t b = (uint8_t)rfidSerial.read();

        rfid_frame_t frame;
        const rfid_event_t ev = rfid_parser_feed(&parser, b, now, &frame);
        if (ev == RFID_EVT_NEW_CARD) {
            presence |= 0x02u;
            lastCardType = frame.card_type;
            char hex[2u * RFID_UID_MAX + 1u];
            const bool reverse = (RFID_UART_REVERSE_MIFARE_UID != 0) && frame.card_type == RFID_CARD_MIFARE;
            rfid_uid_to_hex(frame.uid, frame.uid_len, reverse, hex, sizeof(hex));
            pendingUid = hex;
            LOG_I("[RFID] frame ok type=0x%02X uid=%s\n", frame.card_type, hex);
            return true;
        } else if (ev == RFID_EVT_REPEAT) {
            presence |= 0x02u;
        } else if (ev == RFID_EVT_BAD_FRAME) {
            LOG_W("[RFID] bad frame (#%lu)\n", (unsigned long)parser.frames_bad);
        }
    }

    rfid_parser_tick(&parser, now);
    return false;
}

String rfidReadUid() {
    String uid = pendingUid;
    pendingUid = "";
    return uid;
}

uint8_t rfidGetVersion() {
    // 0x00 = nothing indicates a reader; 0x01 = line idles HIGH; 0x02/0x03 = frames decoded.
    return presence;
}

void rfidSetRawDump(bool on) {
    rawFlush();
    rawDump = on;
    lastRawMs = millis();
    rawLen = 0u;
    rawCount = 0u;
    if (on) {
        const bool high = digitalRead(RFID_UART_RX) != 0;
        Serial.printf("[RFID raw] RX GPIO%d idles %s%s\n", RFID_UART_RX, high ? "HIGH" : "LOW",
                      high ? " (reader driving the line)" : " — no signal: reader unpowered, not wired to this pin, or in Wiegand mode?");
    }
}

void rfidSetBaud(uint32_t baud) {
    rawFlush();
    rfidSerial.updateBaudRate(baud);
    currentBaud = baud;
    while (rfidSerial.available() > 0) (void)rfidSerial.read();   // drop bytes clocked at the old rate
    rfid_parser_init(&parser, RFID_UART_INTERBYTE_MS, RFID_UART_HOLD_GAP_MS);
    rfid_parser_set_format(&parser, RFID_UART_BCC_MODE, RFID_UART_ETX, RFID_UART_STRIP_MIFARE_PAD);
    rfid_parser_set_frame_format(&parser, RFID_UART_FORMAT);
    rawLen = 0u;
    rawCount = 0u;
    Serial.printf("[RFID] UART1 re-clocked to %lu 8N1\n", (unsigned long)baud);
}

uint32_t rfidGetBaud() {
    return currentBaud;
}

void rfidBenchService() {
    if (!rawDump) return;
    const uint32_t now = millis();
    int budget = 64;
    while (budget-- > 0 && rfidSerial.available() > 0) {
        const uint8_t b = (uint8_t)rfidSerial.read();
        rawByte(b, now);
        rfid_frame_t frame;
        const rfid_event_t ev = rfid_parser_feed(&parser, b, now, &frame);
        if (ev == RFID_EVT_NEW_CARD || ev == RFID_EVT_REPEAT) {
            presence |= 0x02u;
            lastCardType = frame.card_type;
            char hex[2u * RFID_UID_MAX + 1u];
            rfid_uid_to_hex(frame.uid, frame.uid_len, false, hex, sizeof(hex));
            rawFlush();
            Serial.printf("[RFID] %s type=0x%02X data_len=%u uid=%s (parser: %s etx=0x%02X)\n",
                          ev == RFID_EVT_NEW_CARD ? "frame ok NEW" : "frame ok REPEAT", frame.card_type,
                          frame.data_len, hex, parser.format == RFID_FORMAT_ASCII_HEX ? "ascii" : "binary", parser.etx);
        } else if (ev == RFID_EVT_BAD_FRAME) {
            rawFlush();
            Serial.printf("[RFID] bad frame #%lu (LEN/BCC/ETX rule mismatch — expected with unconfirmed format)\n",
                          (unsigned long)parser.frames_bad);
        }
    }
    if (rawCount > 0u && (now - lastRawMs) > 20u) {
        rawFlush();
    }
    rfid_parser_tick(&parser, now);
}

bool rfidGetRawDump() {
    return rawDump;
}

RfidUartStats rfidGetStats() {
    RfidUartStats s;
    s.framesOk     = parser.frames_ok;
    s.framesBad    = parser.frames_bad;
    s.framesRepeat = parser.frames_repeat;
    s.resyncs      = parser.resyncs;
    s.noiseBytes   = parser.noise_bytes;
    s.presence     = presence;
    s.lastCardType = lastCardType;
    s.lineHighNow  = digitalRead(RFID_UART_RX) != 0;
    return s;
}
