# One Pork

**Fan package** of [M5PORKCHOP](https://github.com/0ct0sec/M5PORKCHOP) for **M5Cardputer** (ESP32-S3).  
Recommended: **Cardputer ADV**.

| | |
|--|--|
| **Version** | **v0.1.8c (1.6.5.5)** |
| **Upstream base** | M5PORKCHOP **0.1.8c** |
| **This repo** | https://github.com/lexilexiko/OnePork |
| **Upstream** | https://github.com/0ct0sec/M5PORKCHOP |

```
        ^__^
        (oo)\_______
        (__)\       )\/\
            ||----w |
            ||     ||
   fan straw · the real pig is 0ct0's
```

---

## This is a fan mod

**One Pork is a fan-made package.** It is **not** official M5PORKCHOP.

Everything extra here — seasons, wolf, Fruit Run, IR packs, RANK/XP theatre, pwncrack client, zombie pig, monologues, the whole barn aesthetic — is **lexilexiko’s mad fantasy** stacked on top of 0ct0’s original firmware.

- The **original author** is **0ct0**. Donate there: **https://buymeacoffee.com/0ct0**
- **lexilexiko** only packages, paints, and breaks things in entertaining ways.
- If a feature feels unhinged, that’s the point. If a radio trick feels cursed, that’s the ESP32.

License: MIT — Copyright (c) 2025 **0ct0** (`LICENSE`).

---

## What is this?

A **pocket lab** on a Cardputer: Wi-Fi recon, handshake/PMKID capture, GPS wardrive, BLE lab, IR toys, a pig that lives on the screen, and a little XP cult.

**Legal:** only networks you **own** or have **written** permission to test.  
“Because it can” is not a defense. Know your local law.

---

## Quick flash (no build)

1. Download:  
   [`releases/OnePork_v0.1.8c-1.6.5.5_m5cardputer_firmware.bin`](releases/OnePork_v0.1.8c-1.6.5.5_m5cardputer_firmware.bin)
2. Flash with **M5 Launcher** (keeps XP/NVS) or M5Burner / esptool  
3. Reboot. Default boot still warms radio with **OINK ~5s** unless you change Settings → BOOT MODE *and* the firmware honors IDLE.  
4. Notes: [`releases/RELEASE_NOTES_1.6.5.5.md`](releases/RELEASE_NOTES_1.6.5.5.md)

### Build yourself

```text
pio run -e m5cardputer
pio run -t upload -e m5cardputer
```

---

## What’s new in **1.6.5.5**

OINK sniff is back to **upstream [cm0-cardputer](https://github.com/0ct0sec/M5PORKCHOP/tree/cm0-cardputer)**.  
A heap-park of NetworkRecon on HASHES/PWNCRACK hide had killed handshake catch. Rolled back.

| Change | Why |
|--------|-----|
| **Handshake catch = original OINK** | Recon stays up after HASHES. After PWNCRACK upload, Recon `start()`s again. Same as 0ct0. |
| **FRESH** still there | Idle **Z** / SYSTEM → FRESH when *you* want heap, not automatic on loot exit |
| **OINK HUNT** | Settings → RADIO: **KEEP** (default, original) or **RETRY** |
| **OINK bar** | Right: SSID being hit (`[GHOST]` if hidden) |
| HASHES ↔ PWNCRACK no reboot | Still from 1.6.5 (`reserve`/`shrink` crash) |
| Heap | **Enough for now.** Safer reclaim is still in progress — we will not auto-kill Recon again. |

---

## What’s in **1.6.5** (still here)

| Change | Why |
|--------|-----|
| **HASHES ↔ PWNCRACK no longer hard-reboots** | PWNCRACK `reserve` / `shrink_to_fit` on a fragmented heap → `abort()` |
| Cache clear without shrink | WPA-SEC / PWNCRACK caches `clear()` only |
| Safer `NetworkRecon::freeNetworks()` | `shrink_to_fit` no longer inside a spinlock |

Older 1.6 work (pwncrack.org client, zombie skin) stays.  
We did **not** ship the big WiFiService rewrite — it hurt heap/XFER/Blues on device.

The **HEAP: nKB** number will swing (30+ after a good brew, 14–19 after STA). That is largest-contiguous, not “broken RAM”. Upload + OINK capture are the pass/fail.

---

# Capability map

One pig. One radio. Many moods.

```text
ONEPORK
│
├── RADIO
│   ├── OINK            capture HS / PMKID
│   ├── DO NO HAM       passive recon (no attacks)
│   ├── WARHOG          GPS wardrive
│   ├── SPECTRUM        RF view
│   └── PIGGY BLUES     BLE lab
│
├── GAMES / TOYS
│   ├── FRUIT RUN
│   ├── 1RP0RK (IR)
│   └── RANK / FLEXES / GR1ND
│
├── LOOT / CLOUD
│   ├── HASHES → WPA-SEC
│   ├── PWNCRACK → pwncrack.org
│   └── TRACKS → WiGLE
│
├── SCENE
│   ├── AVATAR / PIGLET
│   ├── WOLF
│   ├── TREES / FRUIT
│   ├── WEATHER / SEASONS / FX
│   └── MOOD / MONOLOGUE / RETRO
│
├── WEB
│   ├── FILE XFER
│   └── EVILPIG
│
└── CORE
    ├── NetworkRecon / WiFiUtils
    ├── FRESH (Z)        kill radio, brew, then O
    ├── SD / SDLayout / SDLog
    ├── Config / XP
    ├── GPS / JanusHog (C5)
    └── HeapPolicy / HeapHealth
```

### Hotkeys (from idle)

| Key | Mode | What it does |
|-----|------|----------------|
| **O** | OINK | Deauth + sniff. Captures handshakes / PMKID to SD (`.pcap` + `.22000`). |
| **D** | DO NO HAM | Passive only. Listens, does not attack. |
| **W** | WARHOG | GPS wardrive. Logs CSV + WiGLE tracks. |
| **H** | SPECTRUM | Channel / RSSI view, 2.4 + C5 5 GHz if JanusHog is up. |
| **B** | PIGGY BLUES | BLE advertise / scan lab. Heavy on heap. |
| **F** | FILE XFER | Connects to home Wi-Fi, web UI for SD files. |
| **G** | FRUIT RUN | Jump / collect fruit. Wolf can visit. |
| **I** | 1RP0RK | IR power packs (NA/EU) + custom `/ir` files. |
| **P** | PIGPASS | Offline WPA lab (wordlist / mask on SD). |
| **M** | MICPORK | Mic spectrum toy (ADV codec). |
| **E** | EVILPIG | SoftAP captive portal **lab** (authorized nets only). |
| **S** | FLEXES | Lifetime XP / RANK / GR1ND. |
| **T** | SETTINGS | Skins, Wi-Fi, GPS, C5, boot mode, sound… RADIO → **OINK HUNT**: KEEP (old) or RETRY (reset tries each O). |
| **C** | CHARGING | Low-power charging screen. |
| **Z** | FRESH | Kill Recon / Wi-Fi / BLE, brew heap. Like you just powered on. Then **O**. |
| `` ` `` / ESC | back | Leave a mode → idle / menu. |

### Menu map

| Group | Items |
|-------|--------|
| **ATTACK** | OINK, PIGGY BLUES, EVILPIG, 1RP0RK |
| **RECON** | DO NO HAM, WARHOG, SPECTRUM, MICPORK |
| **LOOT** | HASHES (WPA-SEC), **PWNCRACK**, TRACKS (WiGLE), BOUNTY, PIGPASS |
| **COMMS** | PIGSYNC, BACON, FILE XFER, JANUS HOG |
| **RANK** | FLEXES, BADGES, UNLOCKABLES, FRUIT RUN, D3M4NDS |
| **SYSTEM** | SETTINGS, BOAR BROS, FILES, COREDUMP, DIAG, SD FORMAT, CHARGING, **FRESH**, ABOUT |

### Radio / capture

| Function | Where | Notes |
|----------|--------|--------|
| **OINK** | `modes/oink` | Handshake + PMKID. Uses NetworkRecon promiscuous hop. Saves under `/m5porkchop/handshakes/`. |
| **DO NO HAM** | `modes/do_no_ham` | Same ears, no teeth. Safer for “just look”. |
| **WARHOG** | `modes/warhog` | STA scan + GPS. Writes wardrive CSV + WiGLE CSV. Dual-band extras from C5. |
| **SPECTRUM** | `modes/spectrum` | Live RF picture. Can lock a channel / peek clients. |
| **NetworkRecon** | `core/network_recon` | Shared background scan / hop / network list for OINK, DNH, SPECTRUM. |
| **WSL bypasser** | `core/wsl_bypasser` | Raw frames (deauth etc.) for lab modes. |

### Cloud / loot (three separate pipes)

Do **not** merge these. Different sites, keys, folders.

| Function | Menu | Cloud | SD |
|----------|------|--------|-----|
| **HASHES** | LOOT → HASHES | [WPA-SEC](https://wpa-sec.stanev.org/) | `/m5porkchop/` WPA-SEC key + potfile |
| **PWNCRACK** | LOOT → PWNCRACK | [pwncrack.org](https://pwncrack.org/) | `/m5porkchop/pwncrack/key.txt` |
| **TRACKS** | LOOT → TRACKS | [WiGLE](https://wigle.net/) | WARHOG CSVs + WiGLE creds |

**PWNCRACK (1.6+)**

1. Get a key on pwncrack.org  
2. SD: `/m5porkchop/pwncrack/key.txt`  
3. Same home Wi-Fi as XFER (OTA SSID + password)  
4. Capture `.22000` with **OINK**  
5. **R** load key · **S** upload + potfile · **T** net test · **C** clear upload log  
6. ST column: `[--]` local · `[..]` uploaded · `[OK]` cracked  

If PWNCRACK used to reboot after opening: **1.6.5** stops the `reserve()` crash. If the list is dirty from older builds, delete extra files in `pwncrack/` **except** `key.txt` (via FILES or M5 Launcher).

### Scene / piglet (the fantasy layer)

| Piece | Role |
|-------|------|
| **Avatar** | The pig. Walk, sit, hunt, blush, hog, zombie, retro. |
| **Mood / monologue** | Status lines, insults, timing. |
| **Wolf** | Night visitor. Three night bites unlock **ZOMBIE** skin. Wolf ignores zombie pig. |
| **Trees / fruit** | Ambient trees + Fruit Run pickups. |
| **Weather / seasons / FX** | Rain, snow, leaves, day/night. |
| **RETRO** | B&W film world / pixel rain. |
| **SFX** | Beeps, oinks, sirens. MICPORK and IR steal the speaker briefly. |
| **G0 quiet dark** | Dim / quiet screen. |

Skins: CLASSIC / BLUSH / HOG / **ZOMBIE** (unlock) / RETRO.

### Toys / extra modes

| Function | Idea |
|----------|------|
| **FRUIT RUN** | Chrome-dino energy. Jump, fruit, wolf. |
| **1RP0RK** | IR blast. Built-in NA/EU power packs + `/ir/*.txt`. |
| **BACON** | Beacon hide-and-seek toy. |
| **PIGSYNC** | ESP-NOW sync with another One Pork / Sirloin-style device. |
| **PIGPASS** | Offline crack lab (PBKDF2). Parks the scene so the pig doesn’t eat the heap. |
| **MICPORK** | Microphone spectrum dance (ADV). |
| **EVILPIG** | Clone AP + captive portal. **Lab / authorized only.** |
| **FILE XFER** | Browser file manager over STA. Needs home Wi-Fi. |
| **JANUS HOG** | ESP32-C5 UART coprocessor: 5 GHz scan / extra GPS. |
| **FILES** | Browse / delete SD + SPIFFS. |
| **SD FORMAT** | Nuclear option. Reboot after. |
| **CHARGING** | Sleeps radio/GPS, dim screen. |
| **FRESH** | Kill Recon + STA + BLE, brew the heap hole. Then press **O** like after boot. |
| **COREDUMP / DIAG** | Crash log + heap snapshot. |
| **BOAR BROS** | Networks you refuse to bully. |
| **BOUNTY / BADGES / UNLOCK / D3M4NDS** | XP theatre. |

### Core services (not modes)

| Service | Job |
|---------|-----|
| **Config / NVS** | Settings, skins, boot mode, keys pointers. |
| **XP** | Rank, session bonuses, persistence. |
| **SDLayout** | One folder tree under `/m5porkchop/`. |
| **SDLog** | Text logs on card. |
| **HeapPolicy / HeapHealth / HeapGates** | Thresholds, toasts, TLS gates. |
| **GPS / JanusHog** | Internal UART GPS vs C5. Shared pins → GPS sleeps. |

---

## SD layout

```text
/m5porkchop/
  handshakes/          .pcap / .22000
  pwncrack/
    key.txt
    key.txt.imported
    results / uploaded lists
  wpa-sec/             WPA-SEC key + potfile
  wigle/               WiGLE credentials
  wardriving/          WARHOG CSV / WiGLE tracks
  models/              optional ML
  logs/ crash/ screenshots/
/ir/                   custom IR packs
```

---

## Bugs — what 1.6.5 fixed, what’s still a pig

ESP32-S3 = **one radio**, small heap, **no C++ exceptions**.  
A failed big `vector::reserve` / `shrink_to_fit` → **hard reboot**. We document that instead of pretending the barn is marble.

| ID | Area | Symptom | 1.6.5 |
|----|------|---------|--------|
| **B01** | HASHES → PWNCRACK | Reboot when switching loot menus | **Fixed** — no `reserve`/`shrink_to_fit` |
| **B02** | XFER → PWNCRACK | Reboot after file transfer | **Mitigated** same heap rule; still go easy after TLS |
| **B03** | STA internet | S / XFER “no WiFi” | ⚠️ sometimes — OTA SSID/pass; **OINK** 10–20s or **Z** then **O**; **T** in PWNCRACK |
| **B04** | After BLUES | Net flaky | ⚠️ known — **Z** / **OINK** or reboot |
| **B05** | PWN upload | UP=0 SKIP=N | ⚠️ user — **C** clear log → **S**; same key on site |
| **B06** | PWN ST `[..]` | Waiting for GPU | ✅ expected — **S** later for potfile |
| **B07** | BLUES after OINK | Hard reset on exit (old) | ✅ mitigated in 1.4.6 radio handoff |
| **B08** | EVILPIG portal | Phone ignores portal (old) | ✅ fixed 1.4.1 |
| **B09** | Experimental radio borrow | Broke STA | ✅ not shipped |
| **B10** | ZOMBIE skin | Locked | ✅ intended — 3 night wolf bites |
| **B11** | After WPA-SEC, XFER/heap | Heap 4 KB, PWN `LOW HEAP` | ⚠️ **Z** FRESH then **S**. Do **not** auto-park Recon on HASHES hide — that broke HS catch. |

**Legend:** ⚠️ still a pig · ✅ fixed or intentional

---

## How to report a bug

https://github.com/lexilexiko/OnePork/issues

```text
Version: v0.1.8c (1.6.5.5)
Hardware: M5Cardputer (ADV? C5?)
Steps:
  1. ...
  2. ...
Result:
Expected:
Repro: always / sometimes
After reboot: works / still broken
Notes:
```

**Do not** post live API keys.

Upstream-only bugs: https://github.com/0ct0sec/M5PORKCHOP/issues

---

## Docs map

| File | Contents |
|------|----------|
| **This README** | Fan front door, **full capability map**, bugs, how to report |
| **[FAN.md](FAN.md)** | What appeared in the fan package |
| **[releases/RELEASE_NOTES_1.6.5.5.md](releases/RELEASE_NOTES_1.6.5.5.md)** | 1.6.5.5 OINK sniff restore |
| **[releases/README.md](releases/README.md)** | Prebuilt binary pointer |

---

## Credits

| Who | Role |
|-----|------|
| **0ct0** | Original M5PORKCHOP — **donate:** https://buymeacoffee.com/0ct0 |
| **lexilexiko** | Fan packaging, scenes, toys, and this particular brand of chaos |

Greetz: M5Stack / Cardputer people, anyone who files a clear issue.

*Oink responsibly. This barn is a fanfic with a compiler.*
