// EvilPig Mode - LAB captive portal
// Select AP → show client estimate → softAP clone + optional deauth kicks
// Authorized / lab networks only.
#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>

class EvilPigMode {
public:
    enum class Phase : uint8_t {
        SELECT = 0,  // pick target network
        PORTAL = 1,  // softAP + captive (+ deauth)
        LOOT   = 2   // view captured submits
    };

    static void init();
    static void start();
    static void stop();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isRunning() { return running; }

    static Phase getPhase() { return phase; }
    static uint8_t getClientCount();       // softAP stations when portal
    static uint8_t getTargetClients() { return targetClients; }
    static uint16_t getHitCount() { return hitCount; }
    static uint32_t getDeauthCount() { return deauthCount; }
    static const char* getApSsid() { return apSsid; }
    static uint8_t getChannel() { return apChannel; }
    static const char* getStatus() { return statusMsg; }
    static const char* getLastEvent() { return lastEvent; }
    static bool isDeauthOn() { return deauthOn; }

private:
    enum : uint16_t { DNS_PORT = 53, HTTP_PORT = 80 };
    enum : size_t { kMinFreeHeap = 34000, kMinLargest = 22000 };
    enum : uint8_t { MAX_LIST = 24, VISIBLE = 4, MAX_CATCH = 12 };  // 4 rows + key legend

    struct NetPick {
        uint8_t bssid[6];
        char ssid[33];
        int8_t rssi;
        uint8_t channel;
        uint8_t clients;
        bool hasPmf;
        bool hidden;
    };

    struct Catch {
        char ssid[20];
        char pwShow[28];  // full or truncated for lab view
        uint8_t ch;
    };

    static bool running;
    static Phase phase;
    static Phase phaseBeforeLoot;
    static bool reconWasRunning;
    static bool reconWasPaused;
    static WebServer* server;
    static DNSServer* dns;

    static char apSsid[33];
    static uint8_t apBssid[6];
    static uint8_t apChannel;
    static uint8_t targetClients;
    static bool targetPmf;
    static bool deauthOn;
    static uint16_t hitCount;
    static uint32_t deauthCount;
    static uint32_t lastDeauthMs;
    static char statusMsg[40];
    static char lastEvent[40];
    static uint32_t sessionStart;
    static uint32_t lastListRefresh;

    static NetPick list[MAX_LIST];
    static uint8_t listCount;
    static uint8_t listIdx;
    static uint8_t listScroll;

    static Catch catches[MAX_CATCH];
    static uint8_t catchCount;
    static uint8_t lootScroll;

    static bool ensureHeap();
    static void sortListByRssi();
    static uint8_t staScanIntoList();  // clean STA scan, no OINK required
    static void refreshNetworkList();
    static void ensureListVisible();
    static void applySelection();
    static bool startPortal();
    static void stopServers();
    static void setupRoutes();
    static void sendPortalHeaders();
    static void sendPortalPage();
    static void redirectToRoot();
    static void handleCaptiveProbe();  // captive check → portal (not "online")
    static void handleRoot();
    static void handlePost();
    static void handleNotFound();
    static void servicePortalNet();    // DNS + HTTP pump
    static void saveSubmission(const String& password);
    static void pushCatch(const char* ssid, const String& password, uint8_t ch);
    static void loadCatchesFromSd();
    static void openLoot();
    static void handleInputSelect();
    static void handleInputPortal();
    static void handleInputLoot();
    static void tickDeauth();
    static void setStatus(const char* msg);
    static void drawSelect(M5Canvas& canvas);
    static void drawPortal(M5Canvas& canvas);
    static void drawLoot(M5Canvas& canvas);
};
