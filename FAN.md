# One Pork — fan build notes

**Version: v0.1.8c (1.4.5)**  
- `0.1.8c` — last known upstream M5PORKCHOP base  
- `(1.4.5)` — One Pork fan package revision

This tree is a **fan / community packaging** of **M5PORKCHOP**.

## Credits & donations (read this first)

| Who | Role |
|-----|------|
| **0ct0** | **Original author** of M5PORKCHOP. Owner of the barn. |
| **Donate to the creator** | **https://buymeacoffee.com/0ct0** |
| Fan packaging (this fork) | **lexilexiko** — fan only. Not the original author. |

If you like the pig, **donate to 0ct0**, not to the fan who only polished the straw.

Upstream / releases: https://github.com/0ct0sec/M5PORKCHOP  
This fan repo: https://github.com/lexilexiko/OnePork  
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
- `sd_template/ir/` — sample custom IR code file format

**Not in git / not for the repo** (secrets / local junk):

- `.pio/` build output  
- `Passworld/` wordlists  
- `data/config.json` (local prefs / keys)  

---

## v1.4.5 changelog (latest)

### New

- **RETRO season + pig skin** — black & white film world (pixel rain, mono trees/bushes/bars/clouds)
- **SCENE lab** (settings) — toggle sky / grass / trees / pig / weather / season FX / mood / wolf
- **CPU HUD** — frame load % + ms in top bar free slot (lab only)
- IR **sparkle-storm** anim + **IR_FIRE** SFX (no radio-ring waves)
- Wolf only on **IDLE** / **Fruit Run** (not O/W/B/D work modes)
- SFX layering: low-priority drop, IR mute during TX

### Fixed

- **[B] BLUES** hard-reset on **exit after OINK**  
  - Root cause: `NetworkRecon` re-reserved ~19KB table while NimBLE still held ~20–30KB  
  - With exceptions off, failed `vector::reserve` → `abort()` → reboot  
  - Fix: **`WiFiUtils::releaseBleStack()` first**, then probe-safe re-reserve, then promisc  
  - Same handoff used on BLUES stop, recon start/resume, charging
- CPU HUD no longer crowded on bottom bar

### From 1.4 / 1.4.1 (still in tree)

- 1RP0RK, Fruit Run, RANK/GR1ND, fan XP + ST4TS, seasons, EvilPig portal fix, cold-start BLUES freeNetworks

---

## v1.4 / 1.4.1 changelog (history)

### New (1.4)

- **1RP0RK [I]** — IR power blaster (builtin N4/EU packs + custom `/ir/*.txt` on SD)
- **FRUITRUN [G]** — orchard mini-game (goal, lives, wolf pressure)
- **RANK hub** — FLEXES / BADGES / UNLOCK / FRUITRUN (DEMANDS stays on hotkey **1**)
- **FLEXES → GR1ND tab** — what to do for XP so pig LVL goes up
- **Fan XP + ST4TS** — fruit / wolf scare+hide / PigPass crack / EvilPig catch (NVS)
- **Seasons** — spring cherry, summer apple, autumn old apple, winter fir + FX
- **Wolf / trees / monologue polish** (5s show → 15s silence)
- **Boot / About** — One Pork branding + version stamp

### Fixed (1.4)

- **BLUES [B]** cold start hard-reset/black screen: stop NetworkRecon **and free networks table** (~19KB) before NimBLE (OINK-handoff class fix)
- IR TX mutes speaker during blast (less piezo glitch)
- Scene suspend stays on heavy CPU modes (PigPass / EvilPig / Xfer)

### Fixed (1.4.1)

- **EVILPIG [E]** captive portal auto-open on phones again  
  - probes (`/generate_204`, `/hotspot-detect.html`, etc.) return **portal HTML 200** (not “online”)  
  - fixed gateway **192.168.4.1**, DNS wildcard + TTL 0, multi-pump DNS+HTTP  
  - `Connection: close` + no-cache headers  

### Notes

- Donate **0ct0** first: https://buymeacoffee.com/0ct0  
- Fan packaging only: lexilexiko

---

## Fan features on top of upstream M5PORKCHOP

### Scene / piglet

- Multi-kind **trees** (`trees` + `trees_drops`) — fruit / decor / berry
- Seasonal trees: spring **cherry**, summer **apple**, autumn **old apple**, winter **fir**, **RETRO** mono film trees
- Seasonal produce: cherries, red apples, green apples, cones, berries
- Fallen fruit scrolls with the world + foreground drops
- **Wolf** visitor: chase, bite, sit = peaceful pass-by, stomp/scare (IDLE / Fruit Run only)
- Seasonal FX: snow banks + **trample path**, autumn leaves + tumbleweed, summer butterflies + pollen, spring lightning, **RETRO pixel rain**
- Quiet **RAIN_TICK** SFX; **WOLF_HIT** yelp; **IR_FIRE**
- Monologue bubble: **5s on screen → gone → 15s silence → next line**
- Free-roam walk / jump / sit / play-dead on IDLE
- Scene **suspend** during PigPass / EvilPig / Xfer / WPA-SEC / WiGLE
- **SceneLayers** test lab + CPU HUD (runtime toggles, not NVS)

### Modes

| Key / menu | Mode | Notes |
|------------|------|--------|
| **G** / RANK→FRUITRUN | Fruit Run | Goal, lives, wolf scales |
| **I** / ATTACK→1RP0RK | IR PORK | Builtin N4/EU power packs, custom `/ir/*.txt` |
| **S** / RANK→FLEXES | Flexes | LVL, XP, ST4TS / B00ST / **GR1ND** / W1GL3 |
| RANK→**DEMANDS** | Challenges | Same as hotkey **1** — session trials + XP |
| RANK→BADGES / UNLOCK | Achievements / unlockables | Street cred |
| **B** / ATTACK→BLUES | Piggy Blues | BLE spam; radio handoff frees recon table + BLE release on exit |

### Menus (RPG map)

```
MENU → RANK
  FLEXES    [S]   XP / LVL / T13R / ST4TS / B00ST / GR1ND / W1GL3
  DEMANDS   [1]   session challenges (P1G D3M4NDS)
  BADGES          achievements
  UNLOCK          secret unlockables
  FRUITRUN  [G]   orchard mini-game

MENU → ATTACK
  OINKS, BLUES, EVILPIG, 1RP0RK [I]
```

### IR custom file (SD)

Put files under `/ir/` on the SD card, e.g. `extra_power.txt`:

```text
# PROTO ADDR CMD [name]
NEC 0x00FF 0x45 MyNEC
SAMSUNG 0xE0E0 0x40BF SamsungTV
SONY 0xA90 12 Sony12
```

In 1RP0RK: **E** = pick file, **SPC** = fire, **R** = N4/EU region, **B** = builtin pack.

### Packaging cleanup

- Removed host `test/` tree and unused `native` PlatformIO envs
- SCAN DEFERRED soft heap gate on Hashes/Tracks
- Tightened `.gitignore`

---

## Install firmware (no build)

Prebuilt binary (when shipped):

- `releases/OnePork_v0.1.8c-1.4.5_m5cardputer_firmware.bin`

Flash with M5 Launcher / M5Burner / `esptool`.  
XP is kept if you update via M5 Launcher (do not wipe NVS).

## Build

```text
pio run -e m5cardputer
pio run -t upload -e m5cardputer
```
