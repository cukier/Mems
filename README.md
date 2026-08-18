# Mems — ESP32-C3 IMU + TFT dashboard

Firmware for an ESP32-C3 "Super Mini" board with an integrated 1.44" ST7735
TFT display, reading an LSM6DS3 accelerometer/gyroscope over I2C and showing:

- A live 6-bar histogram (accel X/Y/Z, gyro X/Y/Z), labeled and color-coded.
- Roll, pitch, and yaw, computed on-device from the sensor data (see caveat
  on yaw below).

## Repo layout

```
Mems/
├── docs/            schematics and reference photos for the board
├── src/Mems/        the ESP-IDF firmware project (build from here)
└── lsm6ds3.code-workspace   VS Code multi-root workspace (opens both folders above)
```

## `docs/` folder

Contains the wiring/connection reference for this project: the board
schematic, an I2C wiring diagram, photos of the parts and breadboard
prototype, and a link to the board's product listing.

## Hardware

- Board: ESP32-C3 "Super Mini" with integrated 1.44" ST7735 TFT + case.
  Schematic: [`docs/7036188100_1733311334_org.png`](docs/7036188100_1733311334_org.png).
- IMU: LSM6DS3 breakout, wired over I2C.

### Wiring

| LSM6DS3 pin | ESP32-C3 GPIO |
|-------------|---------------|
| VCC         | 3V3           |
| GND         | GND           |
| SCL         | GPIO20        |
| SDA         | GPIO21        |
| SA0         | leave floating/pulled high → I2C address 0x6B |

The TFT is soldered to the board with a fixed pinout (per the schematic
above) — nothing to wire, but for reference:

| TFT signal | ESP32-C3 GPIO |
|------------|---------------|
| RST        | GPIO5         |
| DC (RS)    | GPIO0         |
| MOSI (SDA) | GPIO4         |
| SCK (SCL)  | GPIO3         |
| CS         | GPIO2         |
| Backlight  | hard-wired to 3.3V, not GPIO-controlled |

## Flashing the firmware (for a friend at home)

You need three things: ESP-IDF installed, this repo, and a USB cable to the
board.

### 1. Install ESP-IDF

This project was built and tested against the **`master`** branch of
ESP-IDF (targeting the `esp32c3` chip). Two ways to get it:

**Option A — VS Code (easiest, GUI-based):**
1. Install [VS Code](https://code.visualstudio.com/).
2. Install the **"Espressif IDF"** extension from the Extensions panel.
3. Run its setup wizard (`ESP-IDF: Configure ESP-IDF Extension`), choosing
   **"master"** as the version to install, and let it download the
   toolchain for you.

**Option B — command line:**
```sh
mkdir -p ~/esp && cd ~/esp
git clone -b master --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c3       # install.bat esp32c3 on Windows
. ./export.sh               # export.bat on Windows — run this in every new terminal
```

### 2. Get this repo

```sh
git clone https://github.com/cukier/Mems.git
cd Mems/src/Mems
```

### 3. Plug in the board

Connect the board to your computer with a USB cable. It exposes a native
USB-Serial-JTAG port, so no separate USB-to-serial driver is normally
needed — it should just show up as a serial device:

- **Linux**: `/dev/ttyACM0` or `/dev/ttyUSB0` (run `ls /dev/tty*` before/after
  plugging in to see which one appears; you may need to be in the `dialout`
  group: `sudo usermod -aG dialout $USER`, then log out/in).
- **macOS**: `/dev/cu.usbmodem*` (run `ls /dev/cu.*`).
- **Windows**: a `COM` port (check Device Manager → Ports).

### 4. Build and flash

From `Mems/src/Mems` (with ESP-IDF's environment sourced/exported):

```sh
idf.py set-target esp32c3
idf.py -p PORT flash monitor
```

Replace `PORT` with the port from step 3 (e.g. `/dev/ttyACM0`, `COM5`). This
builds, flashes, and opens the serial monitor in one go. Press `Ctrl+]` to
exit the monitor.

Using the VS Code extension instead: open `lsm6ds3.code-workspace`, set the
port and target in the bottom status bar, then use the extension's Build →
Flash → Monitor buttons.

### Flashing a prebuilt binary (no ESP-IDF needed)

Every push to `main` rebuilds the firmware and republishes it to the
[`esp32c3-latest` release](https://github.com/cukier/Mems/releases/tag/esp32c3-latest)
as three files: `lsm6ds3_reader.bin`, `bootloader.bin`, `partition-table.bin`.
If a friend just wants to flash the board and doesn't want to install
ESP-IDF, download those three into one folder instead of doing steps 1-2
above.

Flashing them still needs `esptool` (the tool `idf.py flash` itself uses
under the hood). Get it via:

- **pip**: `pip install esptool`
- **Arduino IDE**: if the "esp32 by Espressif Systems" board package is
  already installed (Boards Manager), esptool ships inside it at
  `~/.arduino15/packages/esp32/tools/esptool_py/<version>/esptool`
  (macOS/Linux — `%LOCALAPPDATA%\Arduino15\...` on Windows). Arduino IDE's
  own Upload button runs this same tool internally; it's just not exposed
  for arbitrary `.bin` files through the GUI, so the command line below is
  the way to use it for this project.

Then, from the folder with the three downloaded files:

```sh
esptool --chip esp32c3 --port PORT --baud 460800 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x0     bootloader.bin \
  0x8000  partition-table.bin \
  0x10000 lsm6ds3_reader.bin
```

Replace `PORT` per step 3 above (and `esptool` with the full path if you're
using the Arduino IDE copy). These offsets/settings match what `idf.py
flash` uses itself — see `build/flasher_args.json` in a local build.

### Troubleshooting

- **`idf.py` builds against the wrong Python env** ("configured with X but Y
  is active"): run `idf.py fullclean` then build again.
- **TFT backlight on but blank screen**: check the wiring table above
  against your board revision — the pin mapping is fixed in
  [`src/Mems/main/main.c`](src/Mems/main/main.c) (`TFT_*_GPIO` defines).
- **Nothing on `/dev/ttyUSB0`/`/dev/ttyACM0`**: try unplugging/replugging
  and re-listing `/dev/tty*` (or Device Manager on Windows) to spot the new
  port; some OSes need the CP210x/CH340 driver only if the board uses a
  separate USB-UART chip instead of native USB.

## Notes on the orientation readout

Roll and pitch are gravity-referenced (via a complementary filter) and
stay accurate over time. **Yaw has no absolute reference** — there's no
magnetometer/compass on this board — so it's a pure gyro integration and
will slowly drift. See [`src/Mems/main/orientation.c`](src/Mems/main/orientation.c).
