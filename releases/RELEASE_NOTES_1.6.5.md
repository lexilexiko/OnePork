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

### Docs

Main [README.md](../README.md) now has a **full capability map** (every mode, cloud pipe, scene toy) plus an honest bug table.

## Still a pig

- After a heavy WPA-SEC/TLS session, **OINK** is still the best heap warm-up before XFER.
- BLUES + STA internet can still feel cursed. OINK or reboot.
- One radio. Small heap. We document workarounds instead of shipping the radio rewrite that broke STA.

## From 1.6 (still in this build)

- **PWNCRACK** — [pwncrack.org](https://pwncrack.org/) client, **not** WPA-SEC
- Key: `/m5porkchop/pwncrack/key.txt`
- **S** sync · **T** test · **R** key · **C** clear upload log
- **ZOMBIE** skin after 3 night wolf bites

See [RELEASE_NOTES_1.6.md](RELEASE_NOTES_1.6.md) for the original 1.6 write-up.
