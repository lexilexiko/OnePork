# One Pork — what appeared (fan package)

This file is the **changelog of fan work** on top of upstream **M5PORKCHOP**.

| | |
|--|--|
| **Current** | **v0.1.8c (1.6.5)** |
| **Upstream base** | 0.1.8c |
| **Main README** | [README.md](README.md) — install, **capability map**, bugs table, how to report |
| **Release notes 1.6.5** | [releases/RELEASE_NOTES_1.6.5.md](releases/RELEASE_NOTES_1.6.5.md) |
| **Release notes 1.6** | [releases/RELEASE_NOTES_1.6.md](releases/RELEASE_NOTES_1.6.md) |

---

## Credits

| Who | Role |
|-----|------|
| **0ct0** | Original M5PORKCHOP — **donate:** https://buymeacoffee.com/0ct0 |
| **lexilexiko** | Fan packaging only |

Upstream: https://github.com/0ct0sec/M5PORKCHOP  
Fan: https://github.com/lexilexiko/OnePork  

---

## What the fan package adds (big picture)

Not a rewrite of the barn — **extra straw**:

- Scene: seasons, trees, wolf, RETRO film world, monologue timing  
- Games / toys: Fruit Run, 1RP0RK (IR), RANK / FLEXES / GR1ND XP  
- Cloud: WPA-SEC (upstream-style) + **pwncrack.org** (1.6, separate)  
- Skins: CLASSIC / BLUSH / HOG / **ZOMBIE** (unlock) / RETRO  
- Packaging: One Pork branding, auto-OINK warm-up, prebuilt bins under `releases/`

---

## v1.6.5 — loot menu reboot + FRESH

### Appeared / fixed

| Item | Description |
|------|-------------|
| **HASHES ↔ PWNCRACK** | No more hard reboot when swapping loot menus |
| PWNCRACK list | No `reserve` / `shrink_to_fit` (same rule HASHES already used) |
| Cloud caches | `clear()` only |
| `freeNetworks` | Shrink outside the Wi-Fi spinlock |
| PWNCRACK **S** / **T** | Park pig + Recon, brew heap **before** Wi-Fi (HASHES already did this) |
| Loot **hide** | Recon stays parked — bounce HASHES ↔ PWNCRACK keeps the hole |
| **FRESH** | Idle **Z** or SYSTEM → FRESH. Kill Recon / Wi-Fi / BLE, brew, radio asleep. Then **O**. |
| **OINK HUNT** | Settings → RADIO: KEEP (old) or RETRY (reset tries each O) |
| OINK bar | Right side shows the SSID currently being hit |
| README | Full capability map + fan-mod disclaimer |

Handshake capture is still **OINK**. Heap readout can swing 14–30 KB; that is largest-contiguous, not a second RAM chip.

A larger WiFiService rewrite was **tried and reverted** — it ate heap and broke XFER/Blues. 1.6.5 stays surgical.

---

## v1.6 — pWnCrack + undead pig

### Appeared

| Item | Description |
|------|-------------|
| **PWNCRACK** | Client for [pwncrack.org](https://pwncrack.org/) — separate from WPA-SEC / HASHES |
| Menu | LOOT → PWNCRACK; Integrations key status |
| Key path | `/m5porkchop/pwncrack/key.txt` (→ `.imported` after load) |
| UI | SSID · ST `[--]/[..]`/`[OK]` · TYPE · SIZE · Enter password |
| Keys | **S** sync · **T** net test · **R** key · **C** clear upload log |
| Upload | HTTP multipart; filename as `.hc22000` in request only |
| Boot splash | “NEW: PWNCRACK” + setup steps + ZOMBIE teaser |
| **ZOMBIE skin** | Locked until **3 night wolf bites**; auto-equip on unlock; wolf ignores zombie pig |
| Docs | Honest bugs table in main README |

### Not shipped (tried, rejected)

“Radio borrow/restore” after XFER/HASHES — reduced some reboots but **broke home WiFi connect**.  
1.6 keeps rare reboot; see bugs **B01–B09** in [README.md](README.md).

---

## v1.4.6 — radio kludge

### Problem

OINK promisc + BLUES (NimBLE) + STA/TLS (XFER, WPA-SEC) share one radio and a fragmented heap.  
“Clean” handoffs fixed one path and broke another (`reserve` → reboot).

### What appeared / stayed

| Item | Description |
|------|-------------|
| **1.3-style** NetworkRecon / BLUES handoff | Known-good pattern restored |
| **Auto-OINK ~5s** after boot | Heap/radio warm-up without manual OINK first |
| Default **BOOT MODE = OINK** | NVS IDLE still can warm with OINK |

Kludge — but usable on real hardware.

---

## v1.4.5

| Item | Description |
|------|-------------|
| **RETRO** season + pig skin | B&W film world, pixel rain |
| **SCENE lab** | Toggle sky/grass/trees/pig/weather/FX/mood/wolf |
| **CPU HUD** | Load % in top bar (lab) |
| IR sparkle-storm + IR_FIRE SFX | |
| Wolf only IDLE / Fruit Run | Not on O/W/B/D work modes |
| BLUES exit after OINK | Mitigated hard-reset (BLE release + safe reserve) |
| G0 quiet dark | Screen-off parks scene + mute (quiet feature) |

---

## v1.4 / 1.4.1

### New (1.4)

| Item | Description |
|------|-------------|
| **1RP0RK [I]** | IR power blaster — builtin N4/EU + `/ir/*.txt` on SD |
| **FRUITRUN [G]** | Orchard mini-game — goal, lives, wolves |
| **RANK hub** | FLEXES / BADGES / UNLOCK / FRUITRUN |
| **GR1ND** tab | What to do for XP |
| Fan XP / ST4TS | Fruit, wolf scare/hide, PigPass, EvilPig (NVS) |
| Seasons | Spring cherry / summer apple / autumn / winter fir + FX |
| Boot / About | One Pork branding |

### Fixed (1.4.1)

| Item | Description |
|------|-------------|
| **EVILPIG portal** | Captive portal auto-open on phones again |

### Fixed (1.4)

| Item | Description |
|------|-------------|
| BLUES cold start | Free networks table before NimBLE |
| IR TX | Mute speaker during blast |
| Scene suspend | Heavy modes park pig scene |

---

## Scene / piglet (fan stack)

- Multi-kind trees: fruit / decor / berry + seasonal species  
- Wolf: chase, bite, sit = pass, stomp scare  
- Seasonal FX: snow path, leaves, butterflies, lightning, RETRO pixels  
- Monologue: ~5s on → silence → next  
- Free-roam walk / jump / sit / play-dead on IDLE  
- Scene suspend during PigPass / EvilPig / Xfer / WPA-SEC / WiGLE  

---

## Menu map (fan)

```text
MENU → RANK
  FLEXES   [S]   XP / LVL / ST4TS / GR1ND / …
  DEMANDS  [1]   session challenges
  BADGES / UNLOCK
  FRUITRUN [G]

MENU → ATTACK
  OINK, BLUES, EVILPIG, 1RP0RK [I]

MENU → LOOT
  HASHES     (WPA-SEC)
  PWNCRACK   (pwncrack.org)   ← 1.6
  TRACKS     (WiGLE)
```

---

## IR custom files

Path: `/ir/*.txt` on SD, e.g.:

```text
# PROTO ADDR CMD [name]
NEC 0x00FF 0x45 MyNEC
SAMSUNG 0xE0E0 0x40BF SamsungTV
```

In 1RP0RK: **E** file · **SPC** fire · **R** region · **B** builtin.

---

## Prebuilt binary

```text
releases/OnePork_v0.1.8c-1.6_m5cardputer_firmware.bin
```

Flash: M5 Launcher (prefer) / M5Burner / esptool.

## Build

```text
pio run -e m5cardputer
pio run -t upload -e m5cardputer
```

---

## Report bugs

See the **bugs table** and report template in **[README.md](README.md)**  
→ https://github.com/lexilexiko/OnePork/issues  

Clear steps + version help more than “broken”.
