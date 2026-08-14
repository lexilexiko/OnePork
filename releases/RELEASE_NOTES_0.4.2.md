# 0n3Pork W3b v0.4.2

All-board release. Same web barn. Pick the file that matches the chip.

## Firmware

| File | Chip / board | Flash | Button |
|---|---|---|---|
| `0n3Pork-W3b-v0.4.2-stampc3.bin` | M5Stamp C3 | 4 MB DIO | GPIO 3 |
| `0n3Pork-W3b-v0.4.2-esp32c3.bin` | ESP32-C3 DevKit | 4 MB DIO | GPIO 9 |
| `0n3Pork-W3b-v0.4.2-esp32s3.bin` | ESP32-S3 USB (N4 / SuperMini) | 4 MB DIO | GPIO 0 |
| `0n3Pork-W3b-v0.4.2-esp32s3uart.bin` | ESP32-S3 UART / COM | 4 MB DIO | GPIO 0 |
| `0n3Pork-W3b-v0.4.2-esp32s3-8m.bin` | ESP32-S3 DevKit N8 only | 8 MB QIO | GPIO 0 |
| `0n3Pork-W3b-v0.4.2-esp32.bin` | ESP32 WROOM / DevKit | 4 MB | GPIO 0 |
| `0n3Pork-W3b-v0.4.2-esp32s2.bin` | ESP32-S2 | 4 MB | GPIO 0 |

A C3 file will not boot on S3. The 8 MB S3 file will not boot on a 4 MB S3.

```
pio run -e <env> -t erase
pio run -e <env> -t upload
```

## Default Wi-Fi

- SSID: `0n3Pork W3b`
- Password: `on3pork123`
- UI: http://192.168.4.1 or http://on3pork.local

Set **Aggressive button GPIO** in the web UI if your board is not the default.

## Legal

Use only on networks you own or have written permission to test.
