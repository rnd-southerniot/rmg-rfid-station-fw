#pragma once
#include <Arduino.h>

struct QueuedEvent {
    String eventId;
    String ts;
    String rfidUid;
    String eventType;
    uint8_t retries;
};

void eventQueueInit();
bool eventQueuePush(const String& eventId, const String& ts,
                    const String& rfidUid, const String& eventType);
bool eventQueuePop(QueuedEvent& evt);
int eventQueueSize();
void eventQueueClear();
