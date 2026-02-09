#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>

static TFT_eSPI tft = TFT_eSPI();

// Colors
#define C_BG       TFT_BLACK
#define C_TEXT     TFT_WHITE
#define C_DIM      TFT_DARKGREY
#define C_GREEN    0x07E0
#define C_RED      0xF800
#define C_YELLOW   0xFFE0
#define C_BLUE     0x001F
#define C_HEADER   0x1082  // Dark grey header

// Screen tracking to avoid redundant redraws
enum Screen { SCR_NONE, SCR_BOOT, SCR_CLAIMING, SCR_UNMAPPED, SCR_READY, SCR_SCAN, SCR_QC, SCR_ERROR };
static Screen currentScreen = SCR_NONE;
static String currentReadyId;
static String currentReadyType;

void displayInit() {
    tft.init();
    tft.setRotation(1); // Landscape
    tft.fillScreen(C_BG);
    tft.setTextColor(C_TEXT, C_BG);
    currentScreen = SCR_NONE;
    Serial.println("[Display] Initialized (320x240 landscape)");
}

static void drawHeader(const String& title) {
    tft.fillRect(0, 0, LCD_WIDTH, 30, C_HEADER);
    tft.setTextColor(C_TEXT, C_HEADER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.drawString(title, LCD_WIDTH / 2, 15);
    tft.setTextColor(C_TEXT, C_BG);
}

void displayBootScreen(const String& status) {
    tft.fillScreen(C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.drawString("RMG RFID", LCD_WIDTH / 2, 80);
    tft.setTextFont(2);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("Station Firmware v" FW_VERSION, LCD_WIDTH / 2, 120);
    tft.setTextColor(C_YELLOW, C_BG);
    tft.drawString(status, LCD_WIDTH / 2, 160);
    tft.setTextColor(C_TEXT, C_BG);
    currentScreen = SCR_BOOT;
}

void displayClaimingScreen() {
    tft.fillScreen(C_BG);
    drawHeader("PROVISIONING");
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(C_YELLOW, C_BG);
    tft.drawString("Registering with server...", LCD_WIDTH / 2, 120);
    tft.setTextColor(C_TEXT, C_BG);
    currentScreen = SCR_CLAIMING;
}

void displayUnmappedScreen(const String& mac) {
    tft.fillScreen(C_BG);
    drawHeader("WAITING FOR MAPPING");
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("Station registered but unmapped", LCD_WIDTH / 2, 80);
    tft.drawString("Ask admin to map this station:", LCD_WIDTH / 2, 110);
    tft.setTextColor(C_YELLOW, C_BG);
    tft.setTextFont(4);
    tft.drawString(mac, LCD_WIDTH / 2, 155);
    tft.setTextColor(C_TEXT, C_BG);
    currentScreen = SCR_UNMAPPED;
}

void displayReadyScreen(const String& stationId, const String& lineName, const String& type) {
    // Skip full redraw if already showing same content
    if (currentScreen == SCR_READY && currentReadyId == stationId && currentReadyType == type) {
        return;
    }

    tft.fillScreen(C_BG);
    drawHeader("READY — SCAN TAG");

    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2);

    int y = 50;
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("Station:", 20, y);
    tft.setTextColor(C_TEXT, C_BG);
    tft.setTextFont(4);
    tft.drawString(stationId, 20, y + 25);

    y = 110;
    tft.setTextFont(2);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("Line:", 20, y);
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawString(lineName, 80, y);

    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("Type:", 180, y);
    tft.setTextColor(C_TEXT, C_BG);

    String typeUpper = type;
    typeUpper.toUpperCase();
    tft.drawString(typeUpper, 230, y);

    // Scan prompt
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(C_GREEN, C_BG);
    tft.drawString("Place RFID tag on reader", LCD_WIDTH / 2, 170);
    tft.setTextColor(C_TEXT, C_BG);

    currentScreen = SCR_READY;
    currentReadyId = stationId;
    currentReadyType = type;
}

void displayScanResult(const String& rfidUid, const String& eventType, bool success, const String& message) {
    tft.fillScreen(C_BG);

    uint16_t color = success ? C_GREEN : C_RED;
    if (message == "Unknown Tag") color = C_YELLOW;

    drawHeader(success ? "EVENT SENT" : "SCAN ERROR");

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("UID:", LCD_WIDTH / 2, 55);
    tft.setTextColor(C_TEXT, C_BG);
    // Truncate long UIDs for display
    String displayUid = rfidUid.length() > 20 ? rfidUid.substring(0, 20) + "..." : rfidUid;
    tft.drawString(displayUid, LCD_WIDTH / 2, 75);

    tft.setTextFont(4);
    tft.setTextColor(color, C_BG);
    tft.drawString(message, LCD_WIDTH / 2, 130);

    if (success) {
        tft.setTextFont(2);
        tft.setTextColor(C_DIM, C_BG);
        tft.drawString(eventType, LCD_WIDTH / 2, 175);
    }

    tft.setTextColor(C_TEXT, C_BG);
    currentScreen = SCR_SCAN;
}

void displayQcButtons(const String& rfidUid) {
    tft.fillScreen(C_BG);
    drawHeader("QC INSPECTION");

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("UID: " + rfidUid, LCD_WIDTH / 2, 50);

    // PASS button (left half)
    tft.fillRoundRect(10, 80, 145, 120, 8, C_GREEN);
    tft.setTextColor(C_BG, C_GREEN);
    tft.setTextFont(4);
    tft.drawString("PASS", 82, 140);

    // FAIL button (right half)
    tft.fillRoundRect(165, 80, 145, 120, 8, C_RED);
    tft.setTextColor(C_BG, C_RED);
    tft.drawString("FAIL", 237, 140);

    tft.setTextColor(C_DIM, C_BG);
    tft.setTextFont(1);
    tft.drawString("Tap to select", LCD_WIDTH / 2, 220);
    tft.setTextColor(C_TEXT, C_BG);
    currentScreen = SCR_QC;
}

void displayError(const String& message) {
    tft.fillScreen(C_BG);
    drawHeader("ERROR");
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(C_RED, C_BG);
    tft.drawString(message, LCD_WIDTH / 2, 120);
    tft.setTextColor(C_TEXT, C_BG);
    currentScreen = SCR_ERROR;
}

void displayStatusBar(bool wifiOk, const String& stationId, const String& timeStr) {
    // Only draw on ready screen
    if (currentScreen != SCR_READY) return;

    tft.fillRect(0, LCD_HEIGHT - 20, LCD_WIDTH, 20, C_HEADER);
    tft.setTextFont(1);
    tft.setTextColor(wifiOk ? C_GREEN : C_RED, C_HEADER);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(wifiOk ? "WiFi OK" : "WiFi X", 5, LCD_HEIGHT - 10);

    tft.setTextColor(C_DIM, C_HEADER);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(stationId, LCD_WIDTH / 2, LCD_HEIGHT - 10);

    tft.setTextDatum(MR_DATUM);
    tft.drawString(timeStr, LCD_WIDTH - 5, LCD_HEIGHT - 10);
}

void displayClear() {
    tft.fillScreen(C_BG);
    currentScreen = SCR_NONE;
}
