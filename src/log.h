#pragma once
#include <Arduino.h>

// Log levels: 0=ERROR, 1=WARN, 2=INFO, 3=DEBUG
#define LOG_LVL_ERROR 0
#define LOG_LVL_WARN  1
#define LOG_LVL_INFO  2
#define LOG_LVL_DEBUG 3

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LVL_INFO
#endif

#define LOG_E(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) do { if (LOG_LEVEL >= LOG_LVL_WARN)  Serial.printf(fmt, ##__VA_ARGS__); } while(0)
#define LOG_I(fmt, ...) do { if (LOG_LEVEL >= LOG_LVL_INFO)  Serial.printf(fmt, ##__VA_ARGS__); } while(0)
#define LOG_D(fmt, ...) do { if (LOG_LEVEL >= LOG_LVL_DEBUG) Serial.printf(fmt, ##__VA_ARGS__); } while(0)
