# One Pork v0.1.8c (1.6) — pWnCrack

Fan packaging of **M5PORKCHOP** (upstream base **0.1.8c**).

**Original author: 0ct0** — please donate: https://buymeacoffee.com/0ct0  
Fan packaging: **lexilexiko** — https://github.com/lexilexiko/OnePork

## Flash

- **Binary:** `OnePork_v0.1.8c-1.6_m5cardputer_firmware.bin`
- Target: **M5Cardputer** (ESP32-S3)
- Prefer **M5 Launcher** so XP/NVS is kept

---

## What’s new in 1.6 — pwncrack.org

Separate from **WPA-SEC** (HASHES). Distributed cracking via [pwncrack.org](https://pwncrack.org/).

### Where

- Menu / LOOT → **PWNCRACK**
- Settings → Integrations → **PWNCRACK** key status / load

### Setup

1. Get a key on pwncrack.org (email keygen).
2. Put key on SD:  
   `/m5porkchop/pwncrack/key.txt`  
   (or `key.txt.imported` after first load)
3. In PWNCRACK press **R** if needed → toast KEY OK.
4. Need hashcat **`.22000`** in handshakes (run **OINK** first).
5. Configure home WiFi (same as XFER / OTA SSID+pass).
6. **S** = sync (upload + potfile). **T** = net test (no upload).

### UI (like HASHES)

| Column | Meaning |
|--------|---------|
| SSID | network name |
| ST | `[--]` local · `[..]` uploaded · `[OK]` cracked |
| TYPE | HS / PM |
| SIZE | file size |

- **Enter** — detail; if `[OK]` shows password  
- **S** — upload + potfile  
- **T** — connectivity test (KEY / WIFI / DNS / TCP80 / HTTP / POT)  
- **R** — reload key  
- **C** — clear local “already uploaded” log (re-send files)  
- Bottom bar — rotating control hints  

### Upload details

- Official plugin uses **HTTP** + multipart (`key` + `handshake`).
- Site expects **`.hc22000` name** — firmware renames in the HTTP header only  
  (SD keeps your `.22000`; no third file on disk).
- Passwords appear when GPUs crack offline; **S** again pulls potfile → `[OK]`.

---

## Known bugs & workarounds (honest)

ESP32-S3: **one radio**, small heap, no C++ exceptions → failed `vector::reserve` = **hard reboot**.  
We tried extra “radio borrow/restore” for XFER/HASHES/PWN and **broke STA internet**.  
**1.6 ships without that** — prefer rare reboot over “no WiFi and guess which button”.

### 1) Reboot after WPA-SEC (HASHES) → PWNCRACK

**Symptom:** open HASHES (maybe sync), leave, open PWNCRACK → device restarts.  

**Why:** WPA-SEC TLS + potfile cache + heap fragmentation; next big alloc aborts.  

**Workaround:**

- After reboot everything works — **acceptable**.
- Or wait a bit / visit IDLE before PWNCRACK.
- Or **OINK once** after heavy WPA-SEC (heap brew).
- Avoid HASHES ↔ PWNCRACK spam right after TLS sync.

### 2) Reboot after XFER (File Transfer) → PWNCRACK

**Symptom:** F → exit → PWNCRACK (or S) → reboot.  

**Why:** WebServer/LWIP + recon list + fragmented heap.  

**Workaround:** same as above — reboot is fine; optional OINK; don’t chain F→PWN instantly.

### 3) “No internet” / S won’t connect

**Symptom:** WiFi connect fails in PWN/XFER/WPA-SEC.  

**Workaround:**

1. Check OTA/home SSID+password in settings.  
2. Run **OINK** ~10–20s (warm radio/heap) — classic fix.  
3. **T** in PWNCRACK — see which step dies (WIFI / DNS / TCP / HTTP / KEY).  
4. After BLUES, internet often needs **OINK** or reboot (NimBLE left initialized).

### 4) UP=0 SKIP=N but site empty

**Symptom:** sync “works” but pwncrack.org shows nothing.  

**Workaround:**

- **C** clear upload log → **S** again.  
- Confirm **T** shows `POT 200` / `ALL OK`.  
- Site may be blocked on PC; device can still reach it (HTTP).  
- Login with the **same key** as on the card.

### 5) ST stays `[..]` forever

**Normal** until GPUs crack; press **S** later for potfile. Not a hang.

### 6) Radio / BLUES / OINK (from 1.4.6, still true)

- Auto-OINK ~5s after boot (warm-up).  
- BLUES exit after OINK was the hard fight — 1.3-style handoff + auto-OINK kludge remains.  
- Prefer reboot over experimental radio “fixes” that kill STA.

---

## Still in the tree (1.4.x)

- Auto-OINK / 1.3 radio kludge (1.4.6)  
- RETRO, SCENE lab, G0 quiet dark  
- 1RP0RK, Fruit Run, RANK/GR1ND, fan XP  
- WPA-SEC (HASHES), WiGLE tracks, XFER, EvilPig, etc.

---

## Credits

- **0ct0** — M5PORKCHOP creator — donate first: https://buymeacoffee.com/0ct0  
- **pwncrack.org** / plugin API — community platform (not affiliated)  
- Fan packaging: lexilexiko — `FAN.md` / README section 15  
