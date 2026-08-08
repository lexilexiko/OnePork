# One Pork

**Fan package** of [M5PORKCHOP](https://github.com/0ct0sec/M5PORKCHOP) for **M5Cardputer** (ESP32-S3) (Recommended for Cardputer ADV) .

| | |
|--|--|
| **Version** | **v0.1.8c (1.6)** — *pWnCrack* |
| **Upstream base** | M5PORKCHOP **0.1.8c** |
| **This repo** | https://github.com/lexilexiko/OnePork |
| **Upstream** | https://github.com/0ct0sec/M5PORKCHOP |
| ** 
To fix the PWNCRACK crash/reboot issue, delete all files except the key; you can do this via the launcher or within OnePork itself. ||

```
        ^__^
        (oo)\_______
        (__)\       )\/\
            ||----w |
            ||     ||
   fan straw · real pig is 0ct0's
```

---

## Credits (read first)

| Who | Role |
|-----|------|
| **0ct0** | **Original author** of M5PORKCHOP — owner of the barn |
| **Donate** | **https://buymeacoffee.com/0ct0** ← please donate here |
| **lexilexiko** | Fan packaging only — **not** the original author |

If the pig makes you happy, **support 0ct0**, not only the fan fork.

License: MIT — Copyright (c) 2025 **0ct0** (`LICENSE`).

---

## What is this?

**One Pork** = upstream M5PORKCHOP **plus** fan features (seasons, Fruit Run, IR, RANK/XP polish, **pwncrack.org**, zombie skin unlock, etc.).

It is a **learning / lab tool** for WiFi security research on a pocket keyboard.

**Legal:** only networks you own or have **written** permission to test.  
“Because it can” is not a defense. Know your local law.

---

## Quick flash (no build)

1. Download:  
   [`releases/OnePork_v0.1.8c-1.6_m5cardputer_firmware.bin`](releases/OnePork_v0.1.8c-1.6_m5cardputer_firmware.bin)
2. Flash with **M5 Launcher** (keeps XP/NVS) or M5Burner / esptool  
3. Reboot → ~5s auto-OINK warm-up  
4. Full release notes: [`releases/RELEASE_NOTES_1.6.md`](releases/RELEASE_NOTES_1.6.md)  
5. Fan feature list / history: [`FAN.md`](FAN.md)

### Build yourself

```text
pio run -e m5cardputer
pio run -t upload -e m5cardputer
```

---

## What’s new in **1.6** (highlight)

| Feature | Where | Notes |
|---------|--------|--------|
| **PWNCRACK** | LOOT → PWNCRACK | [pwncrack.org](https://pwncrack.org/) — **not** WPA-SEC |
| Setup | SD `m5porkchop/pwncrack/key.txt` | **R** load key · **S** sync · **T** net test · **C** clear upload log |
| UI | like HASHES | SSID / ST / TYPE / SIZE · Enter = password if `[OK]` |
| Boot splash | on power-on | pwncrack setup + zombie teaser |
| **ZOMBIE skin** | Settings → PIG SKIN | Locked until **3 night wolf bites**; then selectable; wolf won’t bite zombie pig |

Older fan history (1.4 → 1.4.6): see **[FAN.md](FAN.md)**.

---

## Modes (short)

| Key | Mode | Idea |
|-----|------|------|
| **O** | OINK | Capture handshakes / PMKID |
| **D** | DO NO HAM | Passive-only recon |
| **W** | WARHOG | GPS wardrive |
| **H** | SPECTRUM | RF view |
| **B** | PIGGY BLUES | BLE spam (lab) |
| **F** | FILE XFER | Web UI over WiFi |
| **G** | FRUIT RUN | Mini-game |
| **I** | 1RP0RK | IR power packs |
| **P** | PIGPASS | Offline WPA lab crack |
| **M** | MICPORK | Mic spectrometer toy |
| **E** | EVILPIG | SoftAP captive portal (lab) |
| **S** | FLEXES | XP / RANK / GR1ND |
| LOOT | HASHES | WPA-SEC list |
| LOOT | **PWNCRACK** | pwncrack.org |
| MENU | BACON / RANK / … | More modes |

Full upstream-style detail still lives in spirit of M5PORKCHOP; this README is the **fan front door**.

---

## PWNCRACK setup (1.6)

1. Get a key at [pwncrack.org](https://pwncrack.org/)  
2. Put on SD: `/m5porkchop/pwncrack/key.txt`  
3. Set home WiFi (same as XFER / OTA SSID + password)  
4. Capture `.22000` with **OINK**  
5. LOOT → **PWNCRACK** → **R** (key) → **S** (upload + potfile)  
6. **T** = network self-test if something fails  

Details: [`releases/RELEASE_NOTES_1.6.md`](releases/RELEASE_NOTES_1.6.md)

---

## Known bugs & status

ESP32-S3 = **one radio**, small heap, **no C++ exceptions**.  
Failed big `vector::reserve` → **hard reboot**. We ship **honest** workarounds.

| ID | Area | Symptom | Status | Workaround / note |
|----|------|---------|--------|-------------------|
| **B01** | HASHES → PWNCRACK | Reboot after WPA-SEC then open PWN | ⚠️ known | After reboot OK. Or wait / OINK once. Prefer reboot over “radio fix” that killed WiFi |
| **B02** | XFER → PWNCRACK | Reboot after File Transfer then PWN | ⚠️ known | Same as B01 |
| **B03** | STA internet | S / XFER “no WiFi” | ⚠️ sometimes | Check OTA SSID/pass; run **OINK** 10–20s; **T** in PWNCRACK |
| **B04** | After BLUES | Net flaky / need OINK | ⚠️ known | OINK warm-up or reboot; NimBLE leaves heap hot |
| **B05** | PWN upload | UP=0 SKIP=N, site empty | ⚠️ user | **C** clear upload log → **S** again; same key on site |
| **B06** | PWN ST `[..]` forever | Waiting for GPU crack | ✅ expected | Press **S** later for potfile; not a hang |
| **B07** | BLUES after OINK | Hard reset on exit (old) | ✅ mitigated | 1.3 radio handoff + auto-OINK (1.4.6) |
| **B08** | EVILPIG portal | Phone won’t open portal (old) | ✅ fixed | 1.4.1 captive portal probes |
| **B09** | Experimental radio borrow | “Fixed” reboot, **broke STA** | ✅ rejected | Not shipped in 1.6; rare reboot preferred |
| **B10** | ZOMBIE skin | Locked in settings | ✅ intended | 3 night wolf bites unlock |

**Legend:** ⚠️ open / accepted kludge · ✅ fixed or intentional

---

## How to report a bug (please do)

Reports help more than silence. Prefer **GitHub Issues**:  
https://github.com/lexilexiko/OnePork/issues

### Please include

1. **Firmware version** — splash / About: `v0.1.8c (1.6)`  
2. **What you did** — exact path, e.g. `OINK → HASHES S → PWNCRACK`  
3. **What happened** — reboot / no WiFi / freeze / wrong UI  
4. **What you expected**  
5. **How often** — every time / sometimes / once  
6. **Workaround?** — reboot, OINK, etc.  
7. Optional: Serial log, photo of screen, SD layout notes  

### Template

```text
Version: v0.1.8c (1.6)
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

**Do not** post live API keys, WPA-SEC keys, or pwncrack keys in public issues.

Upstream bugs that are pure M5PORKCHOP may also belong at  
https://github.com/0ct0sec/M5PORKCHOP/issues — fan features here first.

---

## SD card (fan paths)

```text
/m5porkchop/
  handshakes/     .pcap / .22000
  pwncrack/
    key.txt           ← pwncrack API key
    key.txt.imported  ← after first load (kept)
    results / uploaded lists (firmware)
  wpa-sec/          WPA-SEC key + potfile
  wigle/            WiGLE credentials
/ir/                custom IR packs (1RP0RK)
```

---

## Docs map

| File | Contents |
|------|----------|
| **This README** | Fan front door, 1.6 highlight, **bugs table**, how to report |
| **[FAN.md](FAN.md)** | What appeared in the fan package (feature history) |
| **[releases/RELEASE_NOTES_1.6.md](releases/RELEASE_NOTES_1.6.md)** | 1.6 flash + pwncrack + workarounds |
| **[releases/README.md](releases/README.md)** | Prebuilt binary pointer |

---

## Greetz

- **0ct0** — M5PORKCHOP  
- M5Stack / Cardputer community  
- Everyone who files a clear bug report  

*Oink responsibly.*
