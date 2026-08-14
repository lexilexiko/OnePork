# 0n3Pork W3b v0.4.1

Multi-board release. Same web barn, pick the binary that matches the chip.

## Flash

Erase first if you came from an older image or saw a boot loop.

| File | Chip | Notes |
|---|---|---|
| `0n3Pork-W3b-v0.4.1-stampc3.bin` | M5Stamp C3 | GPIO 3 |
| `0n3Pork-W3b-v0.4.1-esp32s3.bin` | ESP32-S3 4 MB | GPIO 0, DIO, native USB |

```
pio run -e stampc3 -t erase
pio run -e stampc3 -t upload

pio run -e esp32s3 -t erase
pio run -e esp32s3 -t upload
```

A C3 file will not boot on S3. An 8 MB S3 header will not boot on a 4 MB S3.

Other targets (build yourself): `esp32c3`, `esp32s3uart`, `esp32s3-8m`, `esp32`, `esp32s2`.

## Default Wi-Fi

- SSID: `0n3Pork W3b`
- Password: `on3pork123`
- UI: http://192.168.4.1 or http://on3pork.local

Set **Aggressive button GPIO** in the web UI if your board is not the default.

## Highlights

- One source tree, one binary per chip
- Button GPIO saved in NVS from the web UI
- S3 default is 4 MB + DIO (N4 / SuperMini)
- Light capture on the web, aggressive on the button
- `.pcap` + hashcat `.22000` / `_hs.22000`
- AP + STA, WPA-Sec / Pwncrack sync

## Legal

Use only on networks you own or have written permission to test.
