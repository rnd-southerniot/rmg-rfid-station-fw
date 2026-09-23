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

static HardwareSerial rfidSerial(RFID_UART_NUM);
static rfid_parser_t  parser;
static String         pendingUid;      // latched UID string, consumed by rfidReadUid()
static uint8_t        presence = 0;    // see RfidUartStats::presence
static uint8_t        lastCardType = 0;
static bool           rawDump = false;
static uint32_t       lastRawMs = 0;

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

    LOG_I("[RFID] UART%d reader on RX=%d @%d 8N1, idle line %s\n",
          RFID_UART_NUM, RFID_UART_RX, RFID_UART_BAUD, (presence & 0x01u) ? "HIGH (reader present)" : "LOW (no reader?)");
}

bool rfidCardPresent() {
    if (!pendingUid.isEmpty()) {
        return true;   // latched and not yet consumed
    }

    const uint32_t now = millis();
    int budget = 64;   // bound the work per loop pass
    while (budget-- > 0 && rfidSerial.available() > 0) {
        const uint8_t b = (uint8_t)rfidSerial.read();

        if (rawDump) {
            const uint32_t gap = now - lastRawMs;
            if (gap > 20u) {
                Serial.printf("\n[RFID raw] +%lums:", (unsigned long)gap);
            }
            Serial.printf(" %02X", b);
            lastRawMs = now;
        }

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
    rawDump = on;
    lastRawMs = millis();
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
    return s;
}
