# 0n3Pork W3b

<p align="center">
  <img src="docs/pig.jpg" width="280" alt="0n3Pork pig">
</p>

<p align="center"><b>Your pocket pig.</b> Web UI on any ESP32.</p>

Handshake / PMKID box with a browser instead of a screen.  
Idea, firmware, and the pig: **lexilexiko**.

One tree of source. **One binary per chip** — a C3 `.bin` will not boot on an S3.

| | |
|---|---|
| **Version** | **v0.5.1** |
| **Chips** | ESP32 · S2 · S3 · C3 (M5Stamp C3 included) |
| **UI** | `http://192.168.4.1` · `http://on3pork.local` |
| **Storage** | LittleFS (no SD card) |

```
      ^..^
     (oo  )
    /  >  \~~
   (_    _)
     uu uu
   stamp pig · web barn
```

---

## Legal

Use only on networks you **own** or have **written** permission to test.  
Deauth / handshake capture can be illegal. You are responsible for how you use this firmware.

---

## What it does

1. **Capture** 802.11 EAPOL / PMKID
   - **Light** (web **START light**): same channel, UI stays, PMKID assoc probe
   - **Aggressive** (GPIO in the web UI): hop 1–13, bidirectional kick, **lock 8s on EAPOL** so M2 is not missed
2. Writes **`.pcap`** (WPA-Sec) and hashcat **`.22000` / `_hs.22000`** (`-m 22000`)
3. **AP + STA**: stay on `0n3Pork W3b` while the board joins home Wi-Fi or a cracked network
4. **Sync**
   - [WPA-Sec](https://wpa-sec.stanev.org/) — stream `.pcap` (large files ok), download potfile
   - [pwncrack.org](https://pwncrack.org/) — stream `.22000` (HTTP, TLS if needed), download potfile
5. Web list of cracked keys, **Join** to connect, download captures

Arduino-ESP32 2.0 usually has **no NAT**. AP+STA keeps the UI and gives the board internet. The phone does not get NATed internet through the device.

---

## First connect

After flash, look for this Wi-Fi:

| | |
|---|---|
| **SSID** | `0n3Pork W3b` |
| **Password** | `on3pork123` |
| **URL** | `http://192.168.4.1` |

Change name/password in the web UI (**Save AP name / password**), then reconnect.

Set **Aggressive button GPIO** in the web UI to match your board, then press that button. Press again to stop, then join `0n3Pork W3b` again.

| Board | Env | Default button |
|---|---|---|
| **M5Stamp C3** | `stampc3` | GPIO **3** |
| ESP32-C3 DevKit | `esp32c3` | GPIO **9** (BOOT) |
| ESP32-S3 native USB | `esp32s3` | GPIO **0** |
| ESP32-S3 UART / COM | `esp32s3uart` | GPIO **0** |
| ESP32-S3 DevKit N8 (8 MB) | `esp32s3-8m` | GPIO **0** |
| ESP32 WROOM / DevKit | `esp32` | GPIO **0** |
| ESP32-S2 | `esp32s2` | GPIO **0** |

---

## Flash

### Build + upload (PlatformIO)

```bash
cd "path/to/0n3Pork-W3b"
pio run -e esp32s3          # pick your chip
pio run -e esp32s3 -t erase
pio run -e esp32s3 -t upload
pio run -e esp32s3 -t upload -t monitor
```

Default env is still `stampc3`. If LittleFS fails after a table change, erase then upload again.

**S3 did not boot with the old Stamp C3 file.** That `.bin` is RISC-V. S3 is Xtensa. Build `-e esp32s3` (native USB) or `-e esp32s3uart` (the COM/UART port on many DevKits).

S3 default image is **4 MB + DIO**. That matches SuperMini / N4 modules. If you flash an 8 MB image onto a 4 MB chip it reboots with `Detected size(4096k) smaller than ... (8192k)`. Only use `-e esp32s3-8m` on a real 8 MB DevKit.

### Prebuilt

Release: https://github.com/lexilexiko/0n3Pork-W3b/releases/tag/w3b-v0.5.1

- [M5Stamp C3](https://github.com/lexilexiko/0n3Pork-W3b/releases/download/w3b-v0.5.1/0n3Pork-W3b-v0.5.1-stampc3.bin)
- [ESP32-C3 DevKit](https://github.com/lexilexiko/0n3Pork-W3b/releases/download/w3b-v0.5.1/0n3Pork-W3b-v0.5.1-esp32c3.bin)
- [ESP32-S3 USB 4 MB](https://github.com/lexilexiko/0n3Pork-W3b/releases/download/w3b-v0.5.1/0n3Pork-W3b-v0.5.1-esp32s3.bin)
- [ESP32-S3 UART 4 MB](https://github.com/lexilexiko/0n3Pork-W3b/releases/download/w3b-v0.5.1/0n3Pork-W3b-v0.5.1-esp32s3uart.bin)
- [ESP32-S3 N8 8 MB](https://github.com/lexilexiko/0n3Pork-W3b/releases/download/w3b-v0.5.1/0n3Pork-W3b-v0.5.1-esp32s3-8m.bin)
- [ESP32 classic](https://github.com/lexilexiko/0n3Pork-W3b/releases/download/w3b-v0.5.1/0n3Pork-W3b-v0.5.1-esp32.bin)
- [ESP32-S2](https://github.com/lexilexiko/0n3Pork-W3b/releases/download/w3b-v0.5.1/0n3Pork-W3b-v0.5.1-esp32s2.bin)

S3 USB/UART files are **4 MB**. Use the N8 file only on a real 8 MB DevKit.

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
  cap/             sniffer, pcap, hashcat 22000
  net/             AP / AP+STA, NVS
  storage/         LittleFS
  sync/            wpa-sec, pwncrack
  web/             single-page UI
docs/
  pig.jpg          our pig
  pig-face.jpg     face mark
```

---

## Credits

| Who | Role |
|---|---|
| **lexilexiko** | Idea, firmware, web barn, the pig |

Cardputer cousin: [OnePork](https://github.com/lexilexiko/OnePork).

MIT. See `LICENSE`.
