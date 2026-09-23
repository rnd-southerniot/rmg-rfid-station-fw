#pragma once
// Bench-console hooks for the UART RFID backend (rak3212 only). The application uses the
// backend-neutral API in rfid_reader.h.
#include <Arduino.h>

struct RfidUartStats {
    uint32_t framesOk;
    uint32_t framesBad;
    uint32_t framesRepeat;
    uint32_t resyncs;
    uint32_t noiseBytes;
    uint8_t  presence;      // bit0: RX idled HIGH at init, bit1: a valid frame has been seen
    uint8_t  lastCardType;  // TYPE byte of the last valid frame (0 = none yet)
};

void          rfidSetRawDump(bool on);   // hex-dump every received byte with inter-byte timing
bool          rfidGetRawDump();
RfidUartStats rfidGetStats();
