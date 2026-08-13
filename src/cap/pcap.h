// cap/pcap.h
// PCAP classic file format (LINKTYPE_IEEE802_11_RADIOTAP = 127).
// Compatible with wpa-sec.stanev.org, pwncrack, hcxpcapng, hashcat -m 22000.
//
// Each saved .pcap file has:
//   1 x PCAP file header (24 bytes)
//   N x (PCAP packet header (16 bytes) + radiotap header (8 bytes) + 802.11 frame)
//
// We always write radiotap in front of the raw 802.11 frame because the
// promiscuous callback gives us the 802.11 frame without any radiotap, and
// a lot of tools (hcxpcapng, tshark) expect it.

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace Cap {
namespace Pcap {

// Magic, version, snaplen 65535, linktype 127 (IEEE 802.11 + radiotap).
struct __attribute__((packed)) FileHeader {
    uint32_t magic;        // 0xA1B2C3D4
    uint16_t versionMajor; // 2
    uint16_t versionMinor; // 4
    int32_t  thiszone;     // 0
    uint32_t sigfigs;      // 0
    uint32_t snaplen;      // 65535
    uint32_t linktype;     // 127
};

// Per-packet header. ts is millis() since boot.
struct __attribute__((packed)) PacketHeader {
    uint32_t tsSec;
    uint32_t tsUsec;
    uint32_t inclLen;     // captured length
    uint32_t origLen;     // original length
};

// Minimal radiotap header (8 bytes). No optional fields.
// hashcat and hcxpcapng only need a syntactically valid radiotap to parse
// the 802.11 frame that follows.
static const uint8_t RADIOTAP_HEADER[8] = {
    0x00,       // revision
    0x00,       // pad
    0x08, 0x00, // header length = 8
    0x00, 0x00, 0x00, 0x00  // present flags = 0
};

static const size_t RADIOTAP_LEN = sizeof(RADIOTAP_HEADER);

} // namespace Pcap
} // namespace Cap