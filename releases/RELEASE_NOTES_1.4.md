# One Pork v0.1.8c (1.4)

Fan packaging of **M5PORKCHOP** (upstream base **0.1.8c**).

**Original author: 0ct0** — please donate: https://buymeacoffee.com/0ct0  
Fan packaging only: **lexilexiko** — https://github.com/lexilexiko/OnePork

## Flash

- **Binary:** `OnePork_v0.1.8c-1.4_m5cardputer_firmware.bin` (attached)
- Target: **M5Cardputer** (ESP32-S3)
- Prefer **M5 Launcher** so XP/NVS is kept

## What's new

- **[I] 1RP0RK** — IR power blaster (builtin NA/EU packs + custom `/ir/*.txt` on SD)
- **[G] FRUITRUN** — orchard mini-game (lives, wolf pressure)
- **RANK hub** — FLEXES / DEMANDS / BADGES / UNLOCK / FRUITRUN
- **FLEXES → GR1ND tab** — what to do for XP so pig LVL goes up
- **Seasons** — cherry / apple / old apple / fir + world FX
- **Boot / About** — One Pork branding + **v0.1.8c (1.4)** stamp

## Fixed

- **[B] BLUES** cold-start hard reset / black screen: free NetworkRecon network table before NimBLE (same class of fix as OINK warm-up / XFER handoff)
- IR TX mutes piezo during blast
- Monologue timing: 5s show → 15s silence

## Credits

- **0ct0** — M5PORKCHOP creator (donate first)
- Full notes: `FAN.md` / README section 15
