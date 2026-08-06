# One Pork — fan build notes

**Version: v0.1.8c (1.3)**  
- `0.1.8c` — last known upstream M5PORKCHOP base  
- `(1.3)` — One Pork fan package revision (next changes → **1.4**, then **1.5**…)

This tree is a **fan / community packaging** of **M5PORKCHOP**.

## Credits & donations (read this first)

| Who | Role |
|-----|------|
| **0ct0** | **Original author** of M5PORKCHOP. Owner of the barn. |
| **Donate to the creator** | **https://buymeacoffee.com/0ct0** |
| Fan packaging (this fork) | **lexilexiko** — fan only. Not the original author. |

If you like the pig, **donate to 0ct0**, not to the fan who only polished the straw.

Upstream / releases: https://github.com/0ct0sec/M5PORKCHOP  
License: MIT — Copyright (c) 2025 **0ct0** (`LICENSE`).

---

## What ships in this fan tree (device firmware)

**Built for the device** (`pio run -e m5cardputer`):

- `src/` — all firmware (core, modes, piglet avatar, UI, web)
- `platformio.ini` — `m5cardputer` + `m5cardputer-debug`
- `scripts/pre_build.py` — version stamp at build
- `scripts/build_release.py` — optional release packaging
- `partitions.csv`, `sdkconfig.defaults`
- `patches/` — reference patch (not auto-applied every build)
- `.github/workflows/build.yml` — CI build of firmware
- `README.md`, `LICENSE`, this file

**Not in git / not for the repo** (secrets / local junk):

- `.pio/` build output  
- `Passworld/` wordlists  
- `data/config.json` (local prefs / keys)  
- host unit-test suite (removed — never flashed to the Cardputer)

---

## What this fan pass added / changed (on top of upstream M5PORKCHOP)

Scene / piglet:

- Multi-kind **trees** module (`trees` + `trees_drops`) — fruit / decor / berry
- Seasonal produce (apples, acorns, catkins, cones) + compact red apples
- Fallen fruit scrolls with the world (camera rails) + foreground drops
- **Wolf** visitor: chase, bite → play-dead + 10s control lock; sit → wolf walks past
- Attack hop scares wolf; left-facing attack keeps facing left
- Free-roam walk / jump / sit / play-dead on IDLE
- Scene **suspend** during PigPass / EvilPig / Xfer / WPA-SEC / WiGLE (CPU/heap)

Menus / cloud:

- Hashes / Tracks **SCAN DEFERRED** fix (soft heap gate + free recon list + R retry)
- Same spirit as upstream heap conditioning (OINK still helps TLS)

Cleanup for packaging:

- Removed host `test/` tree and unused `native` PlatformIO envs
- Removed `scripts/oink_timing_sim.py` (dev sim, not firmware)
- Tightened `.gitignore` for a clean upload

---

## Build (fan)

```text
pio run -e m5cardputer
pio run -t upload -e m5cardputer
```

---

## Again: donate the original

**https://buymeacoffee.com/0ct0**

One Pork = fan packaging.  
M5PORKCHOP = 0ct0’s world.  
oink forever.
