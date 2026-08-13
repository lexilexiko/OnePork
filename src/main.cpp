// main.cpp - 0n3Pork W3b entry point.

#include <Arduino.h>
#include <esp_heap_caps.h>
#include "build_info.h"
#include "storage/littlefs_ops.h"
#include "net/ap_sta.h"
#include "cap/sniffer.h"
#include "web/web_server.h"
#include "sync/sync_manager.h"
#include "button/button.h"

void setup() {
    Serial.begin(115200);
    delay(150);
    Serial.println();
    Serial.printf(">>> %s v%s build=%s\n", ON3PORK_NAME,
                  ON3PORK_VERSION, ON3PORK_BUILD);

    // 1. Filesystem
    if (!Storage::begin()) {
        Serial.println("[BOOT] LittleFS FAILED - continuing read-only");
    }

    // 2. WiFi (AP by default, or STA if creds exist in NVS)
    Net::begin();
    Net::Status s = Net::status();
    Serial.printf("[BOOT] mode=%s ssid=%s ip=%s\n",
                  s.mode == Net::Mode::APSTA ? "APSTA" : "AP",
                  s.ssid, s.ip);

    // 3. Capture module (no promiscuous yet, just arm)
    Cap::begin();
    
    // 4. BOOT button for capture toggle
    Button::begin();

    // 5. Web server
    Web::begin();
}

static unsigned long s_lastHeapLog = 0;
static bool s_buttonWasPressed = false;

void loop() {
    Web::loop();
    Cap::loop();   // drains EAPOL ring to LittleFS
    SyncManager::loop();  // handles async WiFi mode switching + sync
    Button::loop();  // service button debounce
    
    // Board button = aggressive only. Press again = stop any capture.
    bool buttonNow = Button::isPressed();
    if (buttonNow && !s_buttonWasPressed) {
        if (Cap::runMode() == Cap::RunMode::Aggressive) {
            Serial.println("[BUTTON] stop aggressive");
            Cap::stop();
        } else {
            Serial.println("[BUTTON] start AGGRESSIVE");
            Cap::startAggressive();
        }
    }
    s_buttonWasPressed = buttonNow;

    if (millis() - s_lastHeapLog > 30000) {
        s_lastHeapLog = millis();
        Serial.printf("[LOOP] heap=%u largest=%u | capture=%s ch=%u eapol=%u written=%u\n",
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                      Cap::runMode() == Cap::RunMode::Aggressive ? "AGG" :
                      (Cap::isRunning() ? "LIGHT" : "OFF"),
                      Cap::counters().currentChannel,
                      Cap::counters().framesEapol,
                      Cap::counters().framesWritten);
    }
    delay(5);
    yield();
}