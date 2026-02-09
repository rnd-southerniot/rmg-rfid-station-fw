#pragma once
#include <Arduino.h>

void touchInit();
bool touchGetPoint(int& x, int& y);

// QC button regions (landscape 320x240)
// PASS button: x=10..155, y=80..200
// FAIL button: x=165..310, y=80..200
enum QcChoice { QC_NONE, QC_PASS_TOUCH, QC_FAIL_TOUCH };
QcChoice touchCheckQcButton(int x, int y);
