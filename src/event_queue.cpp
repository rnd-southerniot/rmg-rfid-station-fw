#include "event_queue.h"
#include "config.h"
#include "log.h"
#include <Preferences.h>

// Simple circular buffer stored in NVS
// Max 50 events, each stored as "eq_N" key with pipe-delimited fields
#define MAX_QUEUE_SIZE 50

static Preferences qPrefs;
static int qHead = 0;  // Next write position
static int qTail = 0;  // Next read position
static int qCount = 0;

void eventQueueInit() {
    qPrefs.begin("evt-queue", false);
    qHead = qPrefs.getInt("head", 0);
    qTail = qPrefs.getInt("tail", 0);
    qCount = qPrefs.getInt("count", 0);
    LOG_I("[Queue] Initialized: %d events pending\n", qCount);
}

bool eventQueuePush(const String& eventId, const String& ts,
                    const String& rfidUid, const String& eventType) {
    if (qCount >= MAX_QUEUE_SIZE) {
        // Drop oldest
        qTail = (qTail + 1) % MAX_QUEUE_SIZE;
        qCount--;
    }

    // Store as pipe-delimited: eventId|ts|rfidUid|eventType|retries
    String value = eventId + "|" + ts + "|" + rfidUid + "|" + eventType + "|0";
    String key = "eq_" + String(qHead);
    qPrefs.putString(key.c_str(), value);

    qHead = (qHead + 1) % MAX_QUEUE_SIZE;
    qCount++;
    qPrefs.putInt("head", qHead);
    qPrefs.putInt("count", qCount);

    LOG_I("[Queue] Pushed event, queue size: %d\n", qCount);
    return true;
}

bool eventQueuePop(QueuedEvent& evt) {
    if (qCount <= 0) return false;

    String key = "eq_" + String(qTail);
    String value = qPrefs.getString(key.c_str(), "");
    if (value.isEmpty()) return false;

    // Parse pipe-delimited
    int p1 = value.indexOf('|');
    int p2 = value.indexOf('|', p1 + 1);
    int p3 = value.indexOf('|', p2 + 1);
    int p4 = value.indexOf('|', p3 + 1);

    evt.eventId = value.substring(0, p1);
    evt.ts = value.substring(p1 + 1, p2);
    evt.rfidUid = value.substring(p2 + 1, p3);
    evt.eventType = value.substring(p3 + 1, p4);
    evt.retries = value.substring(p4 + 1).toInt();

    // Remove from queue
    qPrefs.remove(key.c_str());
    qTail = (qTail + 1) % MAX_QUEUE_SIZE;
    qCount--;
    qPrefs.putInt("tail", qTail);
    qPrefs.putInt("count", qCount);

    return true;
}

int eventQueueSize() {
    return qCount;
}

void eventQueueClear() {
    for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
        String key = "eq_" + String(i);
        qPrefs.remove(key.c_str());
    }
    qHead = 0;
    qTail = 0;
    qCount = 0;
    qPrefs.putInt("head", 0);
    qPrefs.putInt("tail", 0);
    qPrefs.putInt("count", 0);
    LOG_I("[Queue] Cleared\n");
}
