/*
 * serial_console.cpp — see serial_console.h.
 *
 * USB-Serial-JTAG (HWCDC) gotcha: the hardware RX FIFO is 64 bytes, drained by an ISR into a
 * 256-byte queue that we read once per loop pass. Send ONE command per write, <= 96 chars,
 * and wait for the ">" prompt before the next line; pasting a block loses bytes silently.
 */
#include "serial_console.h"

#if BOARD_HAS_CONSOLE

#include "log.h"
#include "lora_link.h"
#include "rfid_uart.h"
#include "event_queue.h"
#include "storage.h"
#include "display.h"
#include "touch.h"
#include "post_results.h"
#include <WiFi.h>
#include <esp_system.h>

namespace {

constexpr size_t CONSOLE_LINE_MAX = 96;   // (LINE_MAX is a POSIX macro in <limits.h>)
char     line[CONSOLE_LINE_MAX + 1];
size_t   lineLen = 0;
bool     overflow = false;

uint32_t lastServiceMs = 0;
uint32_t maxLoopGapMs = 0;
bool     touchEcho = false;
uint32_t lastTouchEchoMs = 0;

void prompt() { Serial.print("> "); }

void cmdHelp() {
    Serial.println("commands:");
    Serial.println("  help                    this list");
    Serial.println("  sys info                chip, memory, MAC/DevEUI, uptime, queue, seq");
    Serial.println("  sys loop                max gap between loop passes since last query (ms)");
    Serial.println("  lora show               link state and counters (keys redacted)");
    Serial.println("  lora join               request an OTAA join now");
    Serial.println("  lora hb                 queue a heartbeat uplink now");
    Serial.println("  lora clear-session yes  wipe nonces+session in NVS and re-join (DESTRUCTIVE)");
    Serial.println("  rfid raw on|off         hex-dump reader bytes with inter-byte timing");
    Serial.println("  rfid stats              parser counters and presence bits");
    Serial.println("  rfid baud <n>           re-clock the reader UART (discovery: 9600/19200/38400/57600/115200)");
    Serial.println("  queue show              pending HTTP replay events");
    Serial.println("  ui qc                   draw the QC PASS/FAIL screen (bench hit-box check)");
    Serial.println("  ui touch on|off         echo touch points and which QC button they hit");
}

void cmdSysInfo() {
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    const LoraStatus ls = loraGetStatus();
    Serial.printf("fw        %s (%s)\n", FW_VERSION, __DATE__);
    Serial.printf("chip      %s rev%u, %u core(s), %lu MHz\n", ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getChipCores(), (unsigned long)ESP.getCpuFreqMHz());
    Serial.printf("flash     %lu bytes   psram %lu bytes (free %lu)\n", (unsigned long)ESP.getFlashChipSize(),
                  (unsigned long)ESP.getPsramSize(), (unsigned long)ESP.getFreePsram());
    Serial.printf("heap      free %lu, min free %lu\n", (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap());
    Serial.printf("mac       %02X:%02X:%02X:%02X:%02X:%02X   deveui %016llX (%s)\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (unsigned long long)ls.devEui,
                  ls.devEuiFromMac ? "from MAC" : "compiled");
    Serial.printf("wifi      %s rssi %d\n", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "down",
                  WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
    Serial.printf("uptime    %lu s\n", (unsigned long)(millis() / 1000u));
    Serial.printf("http q    %d events pending   seq %u\n", eventQueueSize(), storageCurrentSeq());
    if (g_post.valid) {
        Serial.printf("post      LCD=%s(id 0x%04X) LED=%s Buzzer=%s RFID=%s(0x%02X) Touch=%s LoRa=%s\n",
                      g_post.lcd ? "OK" : "FAIL", g_post.lcdId, g_post.led ? "OK" : "FAIL",
                      g_post.buzzer ? "OK" : "FAIL", g_post.rfid ? "OK" : "FAIL", g_post.rfidVer,
                      g_post.touch ? "OK" : "FAIL", g_post.lora ? "OK" : "FAIL");
    }
}

void cmdLoraShow() {
    const LoraStatus s = loraGetStatus();
    Serial.printf("state     %s   link %s (reason %u)\n", loraStateName(s.state), s.linkDown ? "DOWN" : "up", s.offlineReason);
    Serial.printf("deveui    %016llX (%s)   joineui %016llX   appkey %s\n", (unsigned long long)s.devEui,
                  s.devEuiFromMac ? "from MAC" : "compiled", (unsigned long long)s.joinEui, s.credsPresent ? "<set>" : "<zero>");
    Serial.printf("joins     %lu attempts   fcntUp %lu   last rc %d\n", (unsigned long)s.joinAttempts,
                  (unsigned long)s.fcntUp, s.lastRc);
    Serial.printf("uplinks   ok %lu   fail %lu   dropped %lu   queued %u   last %lu s ago\n",
                  (unsigned long)s.uplinksOk, (unsigned long)s.uplinksFail, (unsigned long)s.dropped, s.queued,
                  s.lastUplinkMs ? (unsigned long)((millis() - s.lastUplinkMs) / 1000u) : 0UL);
    Serial.printf("task      stack free (high-water) %lu bytes\n", (unsigned long)s.taskStackFree);
}

void cmdRfidStats() {
    const RfidUartStats s = rfidGetStats();
    Serial.printf("frames    ok %lu   bad %lu   repeat %lu   resyncs %lu   noise bytes %lu\n",
                  (unsigned long)s.framesOk, (unsigned long)s.framesBad, (unsigned long)s.framesRepeat,
                  (unsigned long)s.resyncs, (unsigned long)s.noiseBytes);
    Serial.printf("presence  0x%02X (bit0 line idle HIGH at boot, bit1 frame seen)   last type 0x%02X   raw dump %s\n",
                  s.presence, s.lastCardType, rfidGetRawDump() ? "on" : "off");
    Serial.printf("rx line   GPIO%d is %s right now (%s)   uart %lu 8N1\n", RFID_UART_RX, s.lineHighNow ? "HIGH" : "LOW",
                  s.lineHighNow ? "reader present and idle" : "no reader signal on this pin", (unsigned long)rfidGetBaud());
}

bool streq(const char* a, const char* b) { return strcmp(a, b) == 0; }

void dispatch(char* text) {
    // tokenize in place (max 3 tokens)
    char* tok[3] = { nullptr, nullptr, nullptr };
    int n = 0;
    for (char* p = strtok(text, " \t"); p != nullptr && n < 3; p = strtok(nullptr, " \t")) tok[n++] = p;
    if (n == 0) return;
    const char* a = tok[0];
    const char* b = n > 1 ? tok[1] : "";
    const char* c = n > 2 ? tok[2] : "";

    if (streq(a, "help") || streq(a, "?")) { cmdHelp(); return; }
    if (streq(a, "sys")) {
        if (streq(b, "info")) { cmdSysInfo(); return; }
        if (streq(b, "loop")) { Serial.printf("max loop gap %lu ms (reset)\n", (unsigned long)maxLoopGapMs); maxLoopGapMs = 0; return; }
    }
    if (streq(a, "lora")) {
        if (streq(b, "show")) { cmdLoraShow(); return; }
        if (streq(b, "join")) { loraRequestJoin(); Serial.println("join requested"); return; }
        if (streq(b, "hb"))   { loraRequestHeartbeat(); Serial.println("heartbeat queued"); return; }
        if (streq(b, "clear-session")) {
            if (streq(c, "yes")) { loraClearSession(); Serial.println("clearing session + nonces, re-join follows"); }
            else Serial.println("refused: add 'yes' to confirm (the device must exist in ChirpStack with the same DevEUI)");
            return;
        }
    }
    if (streq(a, "rfid")) {
        if (streq(b, "raw"))   { rfidSetRawDump(streq(c, "on")); Serial.printf("raw dump %s\n", rfidGetRawDump() ? "on" : "off"); return; }
        if (streq(b, "stats")) { cmdRfidStats(); return; }
        if (streq(b, "baud")) {
            const long n = atol(c);
            if (n >= 1200 && n <= 460800) { rfidSetBaud((uint32_t)n); }
            else Serial.println("ERR rfid baud <1200..460800>");
            return;
        }
    }
    if (streq(a, "queue") && streq(b, "show")) { Serial.printf("%d event(s) pending HTTP replay\n", eventQueueSize()); return; }
    if (streq(a, "ui")) {
        if (streq(b, "qc")) { displayQcButtons("BENCH-TEST"); Serial.println("QC screen drawn (state machine unchanged)"); return; }
        if (streq(b, "touch")) { touchEcho = streq(c, "on"); Serial.printf("touch echo %s\n", touchEcho ? "on" : "off"); return; }
    }
    Serial.println("ERR unknown command (try 'help')");
}

} // namespace

void consoleInit() {
    lineLen = 0;
    overflow = false;
    lastServiceMs = millis();
    Serial.println("[Console] bench console ready — type 'help'");
    prompt();
}

void consoleService() {
    rfidBenchService();   // raw dump works in every state, not only when the app polls the reader

    const uint32_t now = millis();
    const uint32_t gap = now - lastServiceMs;
    if (lastServiceMs != 0u && gap > maxLoopGapMs) maxLoopGapMs = gap;
    lastServiceMs = now;

    if (touchEcho && now - lastTouchEchoMs >= 100u) {
        lastTouchEchoMs = now;
        int x, y;
        if (touchGetPoint(x, y)) {
            const QcChoice q = touchCheckQcButton(x, y);
            Serial.printf("touch %3d,%3d -> %s\n", x, y, q == QC_PASS_TOUCH ? "PASS" : q == QC_FAIL_TOUCH ? "FAIL" : "none");
        }
    }

    int budget = 64;
    while (budget-- > 0 && Serial.available() > 0) {
        const char ch = (char)Serial.read();
        if (ch == '\r') continue;
        if (ch == '\n') {
            if (overflow) {
                Serial.println("ERR line too long (max 96 chars)");
            } else {
                line[lineLen] = '\0';
                dispatch(line);
            }
            lineLen = 0;
            overflow = false;
            prompt();
            continue;
        }
        if (lineLen < CONSOLE_LINE_MAX) line[lineLen++] = ch; else overflow = true;
    }
}

#endif // BOARD_HAS_CONSOLE
