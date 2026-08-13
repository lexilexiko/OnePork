# 0n3Pork W3b

Web-first handshake / PMKID pocket box for **M5Stamp C3** (ESP32-C3).  
Fan sibling of [OnePork](https://github.com/lexilexiko/OnePork) (Cardputer). Capture logic follows **OINK** from [M5PORKCHOP](https://github.com/0ct0sec/M5PORKCHOP) by **0ct0**.

| | |
|---|---|
| **Version** | **v0.3.0** |
| **Hardware** | M5Stamp C3, 4 MB flash, USB-C |
| **UI** | browser at `http://192.168.4.1` or `http://on3pork.local` |
| **Storage** | LittleFS (no SD card) |

```
        ^__^
        (oo)\_______
        (__)\       )\/\
            ||----w |
            ||     ||
   stamp-sized pig · web instead of a screen
```

---

## Legal

Use only on networks you **own** or have **written** permission to test.  
Deauth / handshake capture can be illegal. You are responsible for how you use this firmware.

---

## What it does

1. **Capture** 802.11 EAPOL / PMKID
   - **Light** (web **START light**): same channel, UI stays up
   - **Aggressive** (onboard button GPIO3 only): hop 1–13, deauth seen BSSIDs, SSID becomes `0n3Pork AGG`
2. Writes **`.pcap`** (WPA-Sec) and hashcat **`.22000` / `_hs.22000`** (Pwncrack / hashcat `-m 22000`)
3. **AP + STA**: stay on `0n3Pork W3b` while the board joins home Wi-Fi or a cracked network
4. **Sync**
   - [WPA-Sec](https://wpa-sec.stanev.org/) — upload pcap, download potfile
   - [pwncrack.org](https://pwncrack.org/) — upload `.22000`, download potfile
5. Web list of cracked keys, **Join** to connect, download captures

ESP32-C3 Arduino 2.0 has **no NAT**. AP+STA keeps the UI and gives the board internet. The phone does not get NATed internet through the Stamp.

---

## First connect

After flash, look for this Wi-Fi:

| | |
|---|---|
| **SSID** | `0n3Pork W3b` |
| **Password** | `on3pork123` |
| **URL** | `http://192.168.4.1` |

Change name/password in the web UI (**Save AP name / password**), then reconnect.

Onboard **user button is GPIO3** (not GPIO0). Press again to stop aggressive capture, then join `0n3Pork W3b` again.

---

## Flash

### Build + upload (PlatformIO)

```bash
cd "path/to/0n3Pork-W3b"
pio run
pio run -t erase          # needed if the partition table changed
pio run -t upload
pio run -t upload -t monitor
```

Board env: `stampc3` (`esp32-c3-devkitm-1`, 4 MB, custom `partitions.csv`).

If LittleFS fails to mount after a table change, erase then upload again.

### Prebuilt

[**Download v0.3.0 firmware**](https://github.com/lexilexiko/0n3Pork-W3b/releases/download/w3b-v0.3.0/0n3Pork-W3b-v0.3.0-stampc3.bin)  
Release page: https://github.com/lexilexiko/0n3Pork-W3b/releases/tag/w3b-v0.3.0

Flash with esptool / your usual ESP32-C3 tool. Erase flash if you were on an older partition layout.

---

## Workflow

1. Join `0n3Pork W3b` → open the web UI
2. **START light** for a quiet listen, or the **board button** for aggressive
3. Save home Wi-Fi + WPA-Sec / Pwncrack keys
4. **Sync WPA-Sec** / **Sync Pwncrack** (AP stays up)
5. Cracked passwords show in two lists. **Join** puts SSID/pass into AP+STA

---

## Files on flash

```
/handshakes/     AA-BB-CC-DD-EE-FF.pcap
                 AA-BB-CC-DD-EE-FF.22000      (PMKID)
                 AA-BB-CC-DD-EE-FF_hs.22000   (M1+M2)
/results/        wpasec.txt  pwncrack.txt
                 *_uploaded.txt
```

---

## Layout

```
src/
  main.cpp
  button/          GPIO3
  cap/             sniffer, pcap, hc22000 (OINK-compatible lines)
  net/             AP / AP+STA, NVS
  storage/         LittleFS
  sync/            wpa-sec, pwncrack
  web/             single-page UI
```

---

## Credits

| Who | Role |
|---|---|
| **0ct0** | Original [M5PORKCHOP](https://github.com/0ct0sec/M5PORKCHOP) / OINK capture. Donate: [buymeacoffee.com/0ct0](https://buymeacoffee.com/0ct0) |
| **lexilexiko** | [OnePork](https://github.com/lexilexiko/OnePork) on Cardputer, and this Stamp C3 web port |

MIT. See `LICENSE`.
