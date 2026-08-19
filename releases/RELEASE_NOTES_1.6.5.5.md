# One Pork v0.1.8c (1.6.5.5)

Fan packaging of **M5PORKCHOP** (upstream **0.1.8c** / branch [cm0-cardputer](https://github.com/0ct0sec/M5PORKCHOP/tree/cm0-cardputer)).

**Original author: 0ct0** — donate: https://buymeacoffee.com/0ct0  
Fan packaging: **lexilexiko** — https://github.com/lexilexiko/OnePork  

This is a **fan mod**. Extra scenes, toys, RANK theatre, and pwncrack glue are lexilexiko’s mad fantasy on 0ct0’s firmware.

## Flash

- **Binary:** `OnePork_v0.1.8c-1.6.5.5_m5cardputer_firmware.bin`
- Target: **M5Cardputer** (ESP32-S3)
- Prefer **M5 Launcher** so XP/NVS is kept

## What’s new in 1.6.5.5

### Handshake catch restored (same as original)

**Was (1.6.5 heap experiments):** leave HASHES/PWNCRACK → Recon stopped + heap brew → **O** attached to a half-dead radio → **no new handshakes**.

**Now:** OINK / HASHES hide / PWNCRACK disconnect match **upstream cm0-cardputer**:

- Recon stays running after HASHES
- After PWNCRACK Wi-Fi, Recon `start()`s again
- OINK only starts Recon if it is not already running

### Still in this build (from 1.6.5)

- HASHES ↔ PWNCRACK no `reserve`/`shrink_to_fit` reboot
- **FRESH** — idle **Z** or SYSTEM → FRESH (manual heap; then **O**)
- Settings → RADIO → **OINK HUNT**: KEEP (default) / RETRY
- Bottom bar right: SSID OINK is hitting (`[GHOST]` if hidden)

Heap after a heavy TLS session can still look 14–19 KB. That is largest-contiguous. For upload: **Z** then **S**. Do not auto-kill Recon on every loot exit.

See [RELEASE_NOTES_1.6.5.md](RELEASE_NOTES_1.6.5.md) for the 1.6.5 write-up.
