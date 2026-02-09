#include "rfid_reader.h"
#include "config.h"
#include <SPI.h>
#include <MFRC522.h>

// The MFRC522 library uses the global SPI object.
// On ESP32, LCD uses VSPI (default) and RFID uses HSPI.
// We init HSPI on custom pins and swap SPI to point to it before MFRC522 init.
// After init, the MFRC522 continues using SPI (which is our HSPI instance).

static MFRC522 mfrc522(RFID_SS, RFID_RST);

void rfidInit() {
    // Initialize SPI on HSPI pins for RFID
    SPI.end();
    SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);

    mfrc522.PCD_Init();
    delay(10);

    byte ver = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
    Serial.printf("[RFID] MFRC522 version: 0x%02X\n", ver);

    if (ver == 0x00 || ver == 0xFF) {
        Serial.println("[RFID] WARNING: Reader not detected!");
    } else {
        Serial.println("[RFID] Reader initialized");
    }
}

bool rfidCardPresent() {
    return mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial();
}

String rfidReadUid() {
    String uid = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
        if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
        uid += String(mfrc522.uid.uidByte[i], HEX);
    }
    uid.toUpperCase();

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    return uid;
}

uint8_t rfidGetVersion() {
    return mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
}
