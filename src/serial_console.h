#pragma once
// Bench console over USB CDC (BOARD_HAS_CONSOLE builds only). Never prints keys, tokens or
// JWTs; destructive commands need an explicit "yes" argument. Drop the flag for production.
#include <Arduino.h>
#include "config.h"

#if BOARD_HAS_CONSOLE
void consoleInit();
void consoleService();   // call once per loop() pass (non-blocking; also measures loop latency)
#else
static inline void consoleInit() {}
static inline void consoleService() {}
#endif
