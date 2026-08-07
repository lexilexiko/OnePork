# One Pork v0.1.8c (1.4.1)

Fan packaging of **M5PORKCHOP** (upstream base **0.1.8c**).  
**1.4.1** = 1.4 features + post-ship fixes.

**Original author: 0ct0** — please donate: https://buymeacoffee.com/0ct0  
Fan packaging only: **lexilexiko** — https://github.com/lexilexiko/OnePork

## Flash

- **Binary:** `OnePork_v0.1.8c-1.4.1_m5cardputer_firmware.bin` (attached)
- Target: **M5Cardputer** (ESP32-S3)
- Prefer **M5 Launcher** so XP/NVS is kept

## Fixed in 1.4.1

- **[E] EVILPIG** captive portal auto-open on phone again
  - Captive probes return portal HTML (not “internet OK”)
  - Fixed gateway `192.168.4.1`, DNS wildcard, multi-pump DNS+HTTP
  - Connection close + no-cache headers

## Also in this tree (from 1.4 + polish)

### New
- **[I] 1RP0RK** — IR power blaster + custom `/ir/*.txt`
- **[G] FRUITRUN** — orchard mini-game
- **RANK** — FLEXES / BADGES / UNLOCK / FRUITRUN
- **FLEXES GR1ND** + fan XP (fruit, wolf scare/hide, PigPass, EvilPig catch)
- **ST4TS** counters persisted in NVS
- Seasons, wolf, boot/About branding

### Fixed earlier (1.4)
- **[B] BLUES** cold-start reboot (free NetworkRecon table before NimBLE)

## Credits

- **0ct0** — M5PORKCHOP creator (donate first)
- Full notes: `FAN.md` / README section 15
