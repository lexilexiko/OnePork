# One Pork v0.1.8c (1.4.5)

Fan packaging of **M5PORKCHOP** (upstream base **0.1.8c**).  
**1.4.5** = radio handoff fix + RETRO theme + scene lab polish.

**Original author: 0ct0** — please donate: https://buymeacoffee.com/0ct0  
Fan packaging only: **lexilexiko** — https://github.com/lexilexiko/OnePork

## Flash

- **Binary:** `OnePork_v0.1.8c-1.4.5_m5cardputer_firmware.bin` (attached)
- Target: **M5Cardputer** (ESP32-S3)
- Prefer **M5 Launcher** so XP/NVS is kept

## Fixed in 1.4.5

- **[B] BLUES** hard-reset when exiting after **OINK** (handshake search)
  - NimBLE was still holding ~20–30KB while NetworkRecon re-reserved its table
  - Failed `vector::reserve` with exceptions off → `abort()` → reboot
  - Fix: release BLE stack first (`WiFiUtils::releaseBleStack`), then probe-safe re-reserve, then promisc
- CPU HUD relocated to top bar free space

## New in 1.4.5

- **RETRO** season + pig skin — black & white film world (pixel rain, mono trees/bars/clouds)
- **SCENE lab** — layer toggles + CPU% / frame ms HUD
- IR sparkle-storm + IR_FIRE SFX; wolf only on IDLE / Fruit Run
- SFX layering polish

## Also in this tree (1.4 / 1.4.1)

- 1RP0RK, Fruit Run, RANK / GR1ND, fan XP + ST4TS
- EvilPig captive portal phone auto-open
- BLUES cold-start freeNetworks handoff

## Credits

- **0ct0** — M5PORKCHOP creator (donate first)
- Full notes: `FAN.md` / README section 15
