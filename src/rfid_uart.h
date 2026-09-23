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
    bool     lineHighNow;   // live level of the RX pin (a powered reader idles HIGH)
};

// Raw dump takes the reader over: bytes are hex-dumped per frame and fed to the parser for
// diagnostics, but nothing is latched for the application. Serviced by the bench console in
// every state (the application itself only polls the reader in LOGIN/READY).
void          rfidSetRawDump(bool on);
bool          rfidGetRawDump();
void          rfidBenchService();        // call from consoleService(); no-op unless raw dump is on
void          rfidSetBaud(uint32_t baud); // discovery aid: re-clock UART1 at runtime (parser reset)
uint32_t      rfidGetBaud();
RfidUartStats rfidGetStats();
