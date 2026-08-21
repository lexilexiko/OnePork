# Porkchop-Stamp C3 - Project Memory

Single source of truth for the project. Read this first when resuming work.
Updates: append at the bottom (newest at top), never delete old decisions
without explicit user approval.

## 0. Hardware

- Board: M5Stamp C3 (ESP32-C3, RISC-V single core, 4 MB flash, ~320 KB RAM)
- No SD card slot. All persistent storage is on internal flash (LittleFS).
- USB-C: native USB CDC for serial and power.
- No buttons, no screen on Stamp C3 itself. Control is via web UI only.
- ESP32-C3 cannot do STA+AP simultaneously (hardware limit, not a software
  choice). User must switch modes.

## 1. What we are building (the workflow)

Single device that does 3 things across 2 modes:

1. **AP mode + Capture** (default boot state)
   - Stamp opens `Porkchop-AP` (open, no password) at `192.168.4.1`.
   - User connects phone/laptop, opens the web UI.
   - User presses **START capture** (iter 2): Stamp sniffs 802.11 frames in
     promiscuous mode, saves EAPOL handshakes and PMKID into LittleFS as
     `.pcapng` files under `/handshakes/`.
   - User leaves. Comes back, presses **STOP capture**.
   - Power can be cut anytime; files persist in flash.

2. **STA mode + Sync** (manual switch via web UI)
   - User switches to STA, enters home WiFi SSID/pass.
   - Stamp connects. Becomes reachable at `porkchop.local` (mDNS) or by IP.
   - User enters wpa-sec API key (32 hex) and/or pwncrack key.
   - User presses **Sync WPA-Sec** or **Sync Pwncrack**: files upload, potfiles
     (cracked passwords) download to `/results/`.
   - AP is off during this; user is on home WiFi.

3. **AP mode + View** (back to default)
   - User switches back to AP.
   - Web UI now shows the cracked passwords list (BSSID / SSID / password)
     with search box. User can also download pcap files via the web UI.

## 2. Source: where the logic comes from

The Porkchop project on Cardputer
(`F:\roeKT Test\M5PORKCHOP-cm0-cardputer\`) is the reference implementation.
We are lifting and adapting specific files, stripping M5Unified / M5Cardputer
/ Avatar / Mood / NetworkRecon dependencies.

Files lifted so far (none in iter 1, only structure):
- Nothing yet. Iter 1 is a clean slate.

Files to lift later:
- `src/web/wpasec.cpp` / `wpasec.h` -> adapted into `src/sync/wpasec.cpp` (iter 3)
- `src/web/pwncrack.cpp` / `pwncrack.h` -> adapted into `src/sync/pwncrack.cpp` (iter 4)
- `src/modes/oink.cpp` -> look for pcap/pcapng writer + EAPOL handling (iter 2)
- `src/core/sd_layout.h` -> already replaced by `src/storage/littlefs_ops.h`

## 3. Project layout

```
F:\roeKT Test\M5Stamp C3\
  PROJECT.md             this file (project memory)
  README.md              user-facing docs
  platformio.ini         PIO config (board, build flags, libs)
  partitions.csv         nvs + app + storage layout
  .gitignore
  scripts\
    pre_build.py         injects build timestamp into build_info.h
  src\
    main.cpp             entry point
    build_info.h         version + build timestamp (auto-generated)
    net\
      ap_sta.h           AP/STA mode control, NVS config
      ap_sta.cpp
    storage\
      littlefs_ops.h     LittleFS wrappers (replaces SDLayout)
      littlefs_ops.cpp
    web\
      web_server.h       HTTP server, single-page UI
      web_server.cpp
    cap\                 [TODO iter 2] promiscuous sniffer + pcapng writer
    sync\                [TODO iter 3-4] wpa-sec + pwncrack clients
```

## 4. Conventions

- **No Arduino `String` in core code.** Only `const char*` and fixed-size
  `char[]` arrays. This avoids heap fragmentation on a chip with 320 KB RAM.
- **No `printf` with `String` concatenation.** Use `snprintf`.
- **No exceptions, no STL containers that allocate dynamically** in hot paths.
  `std::vector` is allowed in sync code where the size is bounded and we free
  it explicitly with `clear() + shrink_to_fit()` before TLS.
- **All file paths centralized** in `src/storage/littlefs_ops.h`. Do not hardcode
  paths in other modules.
- **All NVS keys centralized** in `src/net/ap_sta.cpp` (and later `src/sync/*`
  for API keys).
- **English-only comments and UI.** No non-ASCII. No emojis in code. Reasons:
  Windows PowerShell `Out-File` mangles encoding, PlatformIO configparser
  crashes on `0x97` (cp1251 byte), and ESP32 build tools occasionally mis-read
  UTF-8 with BOM.
- **Write files with `[IO.File]::WriteAllText(path, content, New UTF8Encoding($false))`**
  in PowerShell. Never use `Out-File` without `-Encoding utf8` and never edit
  a file via `Get-Content | Set-Content` chain if it has any chance of
  containing non-ASCII.

## 5. Iterations status

### Iter 1 - skeleton (DONE)
- [x] `platformio.ini` with esp32-c3-devkitm-1 + ArduinoJson
- [x] `partitions.csv` with nvs + 1.5 MB app + 384 KB storage
- [x] `scripts/pre_build.py` to inject build timestamp
- [x] `src/main.cpp` entry point
- [x] `src/net/ap_sta.h/cpp` AP/STA switching with NVS persistence
- [x] `src/storage/littlefs_ops.h/cpp` LittleFS wrappers
- [x] `src/web/web_server.h/cpp` single-page UI with all sections present
       (Capture and Sync are stubs returning HTTP 503)
- [x] Files are pure ASCII UTF-8 without BOM (after first attempt with mixed
       cyrillic/emoji failed configparser on PlatformIO)

### Iter 2 - promiscuous sniffer + pcap writer (DONE, v0.2.0)
- [x] Create `src/cap/sniffer.h/cpp`
- [x] Use `esp_wifi_set_promiscuous_rx_cb` to grab raw 802.11 frames
- [x] Filter for EAPOL (ethertype 0x888E); PMKID rides in M1
- [x] Classic pcap (linktype 127 + radiotap), not pcapng
- [x] Per-BSSID file naming: `AA-BB-CC-DD-EE-FF.pcap`
- [x] Cap at 50 KB per file, rotate `-2.pcap`..., max 200 files
- [x] Wire `/api/capture/start` and `/api/capture/stop`
- [x] `cap-state` in `/api/status` is a real bool
- [x] Cache one beacon per BSSID and write it first so hashes have ESSID

Reference source:
`F:\roeKT Test\M5PORKCHOP-cm0-cardputer\src\modes\oink.cpp` (start of file
has `struct PCAPHeader`, `writePCAPHeader`, radiotap bytes ~line 50-200
in oink.cpp; currently writes pcap classic, not pcapng).

Decision needed: stay with classic pcap (linktype 127 + radiotap) for
compatibility with wpa-sec/pwncrack, or switch to pcapng. Lean classic pcap
because both servers accept it and the reference implementation already does
it.

### Iter 3 - wpa-sec client (DONE, v0.2.0)
- [x] `src/sync/wpasec.h/cpp` lifted and stripped of M5PORKCHOP deps
- [x] LittleFS + NVS key `wpakey` (32 hex)
- [x] Cache capped at 100 entries; TLS needs ~35 KB contiguous
- [x] `/api/sync` POST `{target:"wpasec"}`

### Iter 4 - pwncrack client (DONE, v0.2.0)
- [x] `src/sync/pwncrack.h/cpp` lifted, HTTP (no TLS)
- [x] NVS key `pwnkey`
- [x] `/api/sync` POST `{target:"pwncrack"}`

### Iter 5 - results browser (DONE, v0.2.0)
- [x] `/api/keys` saves to NVS
- [x] `/api/status` shows real key lengths
- [x] `/api/results` reads both potfiles, `?q=` search
- [x] `/api/handshakes` lists files + sizes
- [x] `/api/handshakes/<name>` downloads (via onNotFound)

## 6. Open decisions to confirm with user

1. **AP password**: open by default, or `porksvin123`?
2. **AP SSID**: `Porkchop-AP` or something else?
3. **mDNS name**: `porkchop.local` or different?
4. **Iter 3 TLS for wpa-sec**: try to fit, or skip wpa-sec entirely and only
   do pwncrack? User said "both" originally but heap reality on C3 is tight.
5. **pcap vs pcapng**: stay with classic pcap (linktype 127 + radiotap)?

## 7. Build & flash

```bash
cd "F:\roeKT Test\M5Stamp C3"
pio run                       # compile only
pio run -t upload             # compile + flash
pio run -t upload -t monitor  # + serial monitor at 115200
```

User runs these and pastes output (text, not screenshot) for me to debug.

## 8. Known risks and gotchas

- **Windows path with space** in `F:\roeKT Test\...` can confuse some PIO
  tooling. If errors mention paths, try moving project to
  `F:\roeKTTest\M5StampC3\` (no spaces) and update this file.
- **PowerShell `Out-File` defaults to UTF-16 LE with BOM** - this corrupts
  files for PlatformIO. Always use `[IO.File]::WriteAllText` with
  `New-Object Text.UTF8Encoding($false)` for any file edit through this
  shell.
- **ESP32-C3 promiscuous mode** drains heap fast if `esp_wifi_set_promiscuous`
  is called while many frames arrive. Need to drop frames in the callback
  if a queue is full, never block.
- **mDNS on AP mode** is sometimes flaky on C3; has been observed to stop
  responding after a few hours. Restart WiFi if it happens.
- **WiFi mode switch is destructive**; switching AP to STA drops all current
  connections, including the web UI you are using. The web UI's POST to
  `/api/wifi/mode` returns before the switch, so the response goes through,
  but the next page load will be on the new mode. User has to reconnect.

## 9. Build/compile error log

- 2026-08-21 - Upload v0.5.1: stream LittleFS to WPA-Sec (TLS) and
  Pwncrack (HTTP then HTTPS). No 100K/200K cap (up to 8 MB / FS).
  Retry on TCP 0, drain reply, live bar in web (file i/n + KB).
  Web::loop pumped while streaming so the UI updates.

- 2026-08-19 - Capture quality v0.5.0 from 0N3P0rK method, no cloud
  change: lock 8s on EAPOL (stop hopping before M2), probe-resp ESSID,
  WDS/0x888E scan, client list + bidirectional deauth burst, PMKID
  auth/assoc probe, promiscuous MGMT+DATA filter, WiFi sleep off.
  Upload can stay PC-side.

- 2026-08-14 - v0.4.2 ships a prebuilt .bin for every PIO env:
  stampc3, esp32c3, esp32s3, esp32s3uart, esp32s3-8m, esp32, esp32s2.

- 2026-08-14 - S3 boot loop: image header said 8 MB, chip
  probed 4 MB (`Detected size(4096k) smaller than ... 8192k`).
  Default `esp32s3` / `esp32s3uart` now 4 MB + DIO. Optional
  `esp32s3-8m` for N8. Erase before reflash (coredump CRC leftover).

- 2026-08-14 - Multi-board v0.4.0. One source tree, separate PIO
  envs: stampc3, esp32c3, esp32s3, esp32s3uart, esp32, esp32s2.
  A C3 binary cannot run on S3. Button GPIO is NVS + web field
  (default per env: Stamp C3=3, C3 DevKit=9, others=0). Board
  name/chip shown in UI. Partition table stays 4 MB-safe.

- 2026-08-13 - Public brand is lexilexiko only. This Stamp C3 web box is
  the user's own project. Do not put 0ct0 / M5PORKCHOP / OINK / donate
  links on README, LICENSE, UI, or GitHub releases. Mascot is our
  pixel pig (docs/pig.jpg, docs/pig-face.jpg, inline SVG in the web
  header). Web UI uses the barn/pink palette. Cardputer cousin OnePork
  may be linked; 0ct0 credits stay off this product.

- 2026-08-13 - Aligned 22000 writer with OnePork/OINK (lexilexiko/OnePork):
  PMKID `WPA*01*...*ESSID***01` as `.22000`, handshake
  `WPA*02*...*00` as `_hs.22000`, PMKID KDE `dd 14 00 0f ac 04`,
  skip zero PMKID, pause deauth 1.2s after M1.

- 2026-08-13 - Capture now also writes hashcat `.hc22000` (PMKID +
  EAPOL M1/M2) next to `.pcap`. Pwncrack uploads only `.hc22000`.
  WPA-Sec potfile parser accepts 3-field, 4-field, and `WPA*...:pass`.
  Old parser required AP:STA at exact offsets so the UI stayed empty
  even when the potfile downloaded. Diagnose now pulls the wpa-sec
  potfile. Removed the extra search box above WPA-Sec keys.

- 2026-08-13 - Renamed product to 0n3Pork W3b. AP SSID `0n3Pork W3b`,
  aggressive SSID `0n3Pork AGG`, mDNS `on3pork.local`. Two capture
  modes: light (web START, same channel, UI stays) and aggressive
  (board button only: hop 1-13 + deauth/disassoc on seen BSSIDs).
  Web cannot start aggressive.

- 2026-08-13 - AP+STA share mode. Old note that C3 cannot do STA+AP
  was wrong: C3 can, same channel. New mode APSTA keeps OnePork AP
  while Stamp joins home or a cracked SSID. Sync uses APSTA so the
  phone is not kicked. Two password panes (wpa-sec / pwncrack) each
  with a Join button. Arduino 2.0.17 C3 SDK has no LWIP NAPT, so the
  phone does not get NATed internet; Stamp itself does (sync works).

- 2026-08-13 - Boot: mount -84 (LFS_ERR_CORRUPT) on empty flash, then
  `disableCore0WDT` fail on C3 and noisy `no permits for creation`.
  Empty partition must be formatted; Arduino `LittleFS.format()` uses
  Core0 idle WDT which does not exist on unicore C3. Format via
  `esp_littlefs_format` + `disableLoopWDT`. `exists()` before mkdir
  was a false error (open without create).

- 2026-08-13 - Boot: `partition "spiffs" could not be found` / LittleFS
  format Error 261. Cause: partitions.csv labeled the FS partition
  `storage`. Arduino LittleFS.begin() looks up label `spiffs`, not the
  subtype. Renamed the partition to `spiffs`. Erase + reflash required.

- 2026-08-13 - Full correctness pass (v0.2.0). Runtime bugs from audit
  fixed, stubs replaced so the device follows the intended workflow:
  - Button pin is GPIO3 (M5Stamp C3 user button), not GPIO0.
  - SyncManager phase timer only changes on phase enter. Sync can
    finish and return to AP. Failure is no longer overwritten as success.
  - Sniffer writes one .pcap per BSSID, switches files on BSSID change,
    rotates at 50 KB (`-2.pcap` ...), drains the ring on STOP.
  - Filenames are `AA-BB-CC-DD-EE-FF.pcap` (23 bytes with NUL).
  - First packet in a new pcap is a cached beacon for that BSSID so
    hcxpcapng/hashcat -m 22000 have an ESSID.
  - Temporary AP SSID (`OnePork Stop`) is not written to NVS.
  - `partitions.csv` is now used (1.5 MB app + 2.4 MB LittleFS).
    Reflash with erase if you were on default.csv.
  - Wipe remounts LittleFS and recreates `/handshakes` + `/results`.
  - Handshake list uses basename; download is handled in onNotFound
    (Arduino WebServer has no `/path/*` wildcard).
  - `/api/keys` stores wpa-sec (32 hex) and pwncrack keys in NVS.
  - `/api/results` reads both potfiles. `/api/upload` writes binary
    via the WebServer upload handler.
  - wpa-sec: real TLS multipart upload + potfile GET.
  - pwncrack: real HTTP upload + potfile GET.
  - Diagnose does DNS+TCP when in STA.

- 2026-08-11 - first attempt at files contained mixed cp1251/UTF-8 from
  PowerShell `Out-File` plus non-ASCII (cyrillic, emoji). PIO configparser
  crashed with `UnicodeDecodeError: 0x97`. Fix: rewrote all files as pure
  ASCII UTF-8 no-BOM.
- 2026-08-11 - Iter 2 (handshake capture) added. New module `src/cap/`:
  - `pcap.h` - PCAP classic file header + packet header structs,
    radiotap header constant (8 bytes, no optional fields).
  - `sniffer.h/cpp` - promiscuous RX callback. Filters for data frames
    carrying LLC/SNAP ethertype 0x888E (EAPOL), drops them into a static
    SPSC ring (8 slots x 320 bytes, ~2.5 KB) without malloc. `loop()`
    drains the ring and appends each EAPOL frame to
    `/handshakes/AA-BB-CC-DD-EE-FF.pcap` (one file per BSSID, linktype 127
    + radiotap). Rotates at 50 KB per file. Channel hops every 250 ms
    over 1,6,11,2,3,4,5,7,8,9,10,12,13.
  - Decisions:
    * Classic pcap, not pcapng (both wpa-sec and pwncrack accept it,
      and hcxpcapng can convert if needed).
    * No beacon frames saved - EAPOL alone is enough for
      `hashcat -m 22000` (after `hcxpcapngtool` conversion). Beacons
      would bloat the file ~10x and PCAP rotation logic.
    * No deauth, no auto-attack, no targeting - user just presses
      START and gets every handshake in range. Stamp C3 with 320 KB
      RAM cannot run a state machine like M5PORKCHOP's OinkMode
      (which has deauth, mood, XP, scoring, channels, PMKID hunting
      all at once). KISS.
  - Compiles: RAM 13.5% (44 KB), Flash 66.3% (868 KB / 1.3 MB).


- 2026-08-11 - first attempt at files contained mixed cp1251/UTF-8 from
  PowerShell `Out-File` plus non-ASCII (cyrillic, emoji). PIO configparser
  crashed with `UnicodeDecodeError: 0x97`. Fix: rewrote all files as pure
  ASCII UTF-8 no-BOM.