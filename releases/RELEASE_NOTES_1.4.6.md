# One Pork v0.1.8c (1.4.6) — fix / workaround

Fan packaging of **M5PORKCHOP** (upstream base **0.1.8c**).

**Original author: 0ct0** — please donate: https://buymeacoffee.com/0ct0  
Fan packaging: **lexilexiko** — https://github.com/lexilexiko/OnePork

## Flash

- **Binary:** `OnePork_v0.1.8c-1.4.6_m5cardputer_firmware.bin` (attached)
- Target: **M5Cardputer** (ESP32-S3)
- Prefer **M5 Launcher** so XP/NVS is kept

## The hard problem (ESP32-S3 radio / heap)

On this board WiFi promiscuous (OINK), BLE (BLUES), and STA+TLS (XFER / WPA-SEC)
fight over the same radio and a tiny fragmented heap. Getting handoffs “perfect”
is genuinely hard — NimBLE, promisc, and `WiFi.begin` do not compose cleanly,
and a failed `vector::reserve` with exceptions off is a **hard reset**.

We tried several “clean” handoff designs in 1.4.x. Some fixed one path and
broke another. Fighting the ESP-IDF stack end-to-end was costing stability.

## Workaround in 1.4.6 (works for us)

**Kludge that seems to work:**

1. **Radio stack restored to the known-good 1.3 pattern** for BLUES / NetworkRecon
   (no experimental freeNetworks / multi-layer BLE teardown on every transition).
2. **Auto-OINK after boot** (~5s) — the old “OINK bounce” warms WiFi/heap so
   XFER, WPA-SEC, and BLUES behave more like after a manual OINK first.
   - Default **BOOT MODE = OINK**
   - If NVS still has IDLE → still warm-up with OINK
   - DNOHAM / WARHOG from settings still respected
   - Crash boot-guard still forces IDLE

Not elegant. Not a full rewrite of the radio owner. **But OINK + net + B
are usable again** on our hardware.

## Still in the tree (from 1.4 / 1.4.5)

- RETRO season/pig, SCENE lab, G0 screen-off mute (quiet dark)
- 1RP0RK, Fruit Run, RANK / GR1ND, fan XP
- EvilPig portal fixes, etc.

## Credits

- **0ct0** — M5PORKCHOP creator (donate first)
- Full fan notes: `FAN.md` / README section 15
