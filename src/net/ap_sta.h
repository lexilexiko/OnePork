// net/ap_sta.h
// AP, STA, or AP+STA (same radio / same channel).

#pragma once

#include <Arduino.h>

namespace Net {

enum class Mode {
    AP,
    STA,
    APSTA,  // stay on 0n3Pork W3b while connected to another WiFi
};

struct Status {
    Mode mode;
    bool connected;       // AP: AP up; STA/APSTA: STA associated
    bool staConnected;
    bool napt;
    char ssid[33];        // primary: AP ssid or STA ssid
    char apSsid[33];
    char staSsidShow[33];
    char ip[16];          // AP IP, or STA IP if STA-only
    char apIp[16];
    char staIp[16];
    char rssi[8];
    uint8_t apClients;
    char mac[18];
};

struct Cfg {
    Mode mode;
    char apSsid[33];
    char apPass[65];
    uint8_t apChannel;
    char staSsid[33];
    char staPass[65];
    char wpaSecKey[33];
    char pwncrackKey[65];
};

static const char* const AP_SSID_IDLE = "0n3Pork W3b";
static const char* const AP_SSID_CAP  = "0n3Pork AGG";
static const char* const AP_PASS_DEFAULT = "on3pork123";

void begin();
const Cfg& cfg();
Status status();

bool setMode(Mode m);
bool setAp(const char* ssid, const char* pass, uint8_t channel);
bool setSta(const char* ssid, const char* pass);
void clearSta();
bool hasStaCreds();
bool staLinked();   // STA associated (STA or APSTA)

bool setWpaSecKey(const char* key);
bool setPwncrackKey(const char* key);

void save();
void loadDefaults();

bool setApSsidTemporary(const char* ssid);
bool restoreApRadio();

} // namespace Net
