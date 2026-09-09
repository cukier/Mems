# INVOKE Band

A classroom quiz system. The teacher sends a question (statement + 4 answers +
a time limit) to a set of ESP32-C3 wristbands; each student tilts the band to
pick an answer, watching the choice update live on the band's screen; when time
runs out the band shows its answer and reports it back; the teacher sees every
band's answer on a live board.

## Repo layout

```
Mems/
├── firmware/                 ESP-IDF firmware for the wristband (build from here)
├── apps/
│   ├── invoke-web/           teacher app — Vite/React + Fastify/SQLite, docker-compose
│   └── invoke-console/       minimal Web Bluetooth bench tool for the BLE transport
├── docs/                     board schematic + BLE protocol spec
├── soul.md                   living design notes
└── Mems.code-workspace       VS Code multi-root workspace
```

Protocol: [`docs/INVOKE_BLE_ESPECIFICACAO.md`](docs/INVOKE_BLE_ESPECIFICACAO.md).

## Teacher app

```sh
cd apps/invoke-web
docker compose up --build
```

App on `https://localhost:5173` (self-signed cert — Web Bluetooth needs a
secure context; from an Android phone use `https://<LAN-IP>:5173`). API on
`:8787`. See [`apps/invoke-web/README.md`](apps/invoke-web/README.md).

## Firmware

Board: ESP32-C3 "Super Mini" with an integrated 1.44" ST7735 TFT + case.
Schematic: [`docs/7036188100_1733311334_org.png`](docs/7036188100_1733311334_org.png).
IMU: LSM6DS3 over I2C.

### Wiring

| LSM6DS3 pin | ESP32-C3 GPIO |
|-------------|---------------|
| VCC         | 3V3           |
| GND         | GND           |
| SCL         | GPIO20        |
| SDA         | GPIO21        |
| SA0         | float / pull high → I2C address 0x6B |

The TFT is soldered to the board with a fixed pinout (per the schematic):
RST=GPIO5, DC=GPIO0, MOSI=GPIO4, SCK=GPIO3, CS=GPIO2, backlight hard-wired to
3.3 V. Pins are defined in [`firmware/main/main.c`](firmware/main/main.c).

### Each band's number

Every band has a unique number 1..64 in NVS (key `band`, namespace `invoke`).
It drives the BLE name `INVOKE-xx` and the `n` field of answers. Until
provisioned, a band falls back to `CONFIG_INVOKE_DEFAULT_BAND_NUM` (menuconfig →
"INVOKE Band Config").

### Build and flash (with ESP-IDF)

Built against ESP-IDF **`master`**, target `esp32c3`.

```sh
git clone https://github.com/cukier/Mems.git
cd Mems/firmware
idf.py set-target esp32c3
idf.py -p PORT flash monitor
```

`PORT` = `/dev/ttyACM0` (Linux, add yourself to `dialout`), `/dev/cu.usbmodem*`
(macOS), or a `COM` port (Windows). `Ctrl+]` exits the monitor.

### Flash a prebuilt binary (no ESP-IDF)

Every push to `main` republishes the
[`esp32c3-latest` release](https://github.com/cukier/Mems/releases/tag/esp32c3-latest)
with `lsm6ds3_reader.bin`, `bootloader.bin`, `partition-table.bin`. Download the
three into one folder, then:

```sh
pip install esptool
esptool --chip esp32c3 --port PORT --baud 460800 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x0     bootloader.bin \
  0x8000  partition-table.bin \
  0x10000 lsm6ds3_reader.bin
```

(The ESP-IDF project is still named `lsm6ds3_reader` for continuity, hence the
binary name.)

### Troubleshooting

- **`idf.py` wrong Python env** ("configured with X but Y is active"):
  `idf.py fullclean`, then rebuild.
- **TFT backlight on but blank**: check the pin table against your board
  revision (`TFT_*_GPIO` in `firmware/main/main.c`).
- **No serial port**: unplug/replug and re-list `/dev/tty*`; the board uses
  native USB-Serial-JTAG (no CP210x/CH340 driver needed).
- **Band connects then drops / picker doesn't list it**: see
  [`docs/FIRMWARE_BLE_STATUS.md`](docs/FIRMWARE_BLE_STATUS.md) and the spec's
  §6 checklist.
