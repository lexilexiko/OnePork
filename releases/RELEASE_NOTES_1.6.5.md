# One Pork v0.1.8c (1.6.5)

Fan packaging of **M5PORKCHOP** (upstream **0.1.8c**).

**Original author: 0ct0** — donate: https://buymeacoffee.com/0ct0  
Fan packaging: **lexilexiko** — https://github.com/lexilexiko/OnePork  

This is a **fan mod**. Extra scenes, toys, RANK theatre, and pwncrack glue are lexilexiko’s mad fantasy on 0ct0’s firmware.

## Flash

- **Binary:** `OnePork_v0.1.8c-1.6.5_m5cardputer_firmware.bin`
- Target: **M5Cardputer** (ESP32-S3)
- Prefer **M5 Launcher** so XP/NVS is kept

## What’s new in 1.6.5

### HASHES ↔ PWNCRACK reboot

**Was:** leave HASHES (WPA-SEC), open PWNCRACK (or the other way) → device reboots.

**Why:** PWNCRACK `show()` called `metas.reserve()`, `hide()` called `shrink_to_fit()`, and both cloud caches shrank on a fragmented heap. No C++ exceptions → `abort()`.

**Now:**

- PWNCRACK does not `reserve` / `shrink_to_fit` the file list
- WPA-SEC / PWNCRACK caches only `clear()`
- `NetworkRecon::freeNetworks()` no longer shrinks inside a spinlock
- Opening PWNCRACK stops adding rows if the largest free block is tiny (no crash)

### Cloud heap (same version, later build)

**Was:** WPA-SEC upload showed HEAP ~24, then PWNCRACK S said `LOW HEAP 4`.

**Why:** the 24 KB was one coalesced hole after HASHES brew. Leaving the menu started **NetworkRecon** again (~19 KB list). PWNCRACK then connected Wi-Fi *before* brewing.

**Now:**

- PWNCRACK **S** / **T** parks the pig + Recon and brews **before** Wi-Fi
- Leaving HASHES / PWNCRACK does **not** restart Recon (OINK / DNH / SPECTRUM still start it)
- **FRESH** — idle **Z**, or SYSTEM → FRESH: kill Recon + Wi-Fi + BLE, brew, radio asleep. Then press **O** the old way.

Heap on screen may still read **30+** after a good brew and **14–19** after STA. That is fine. **Upload works. Handshake capture is still OINK.**

### Docs

Main [README.md](../README.md) has a **full capability map** (every mode, cloud pipe, scene toy) plus an honest bug table.

## Still a pig

- After Blues, **Z** then **O** (or a reboot) if the net feels cursed.
- One radio. Small heap. We document workarounds instead of shipping the radio rewrite that broke STA.

## From 1.6 (still in this build)

- **PWNCRACK** — [pwncrack.org](https://pwncrack.org/) client, **not** WPA-SEC
- Key: `/m5porkchop/pwncrack/key.txt`
- **S** sync · **T** test · **R** key · **C** clear upload log
- **ZOMBIE** skin after 3 night wolf bites

See [RELEASE_NOTES_1.6.md](RELEASE_NOTES_1.6.md) for the original 1.6 write-up.
