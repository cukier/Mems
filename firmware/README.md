# INVOKE Band firmware (ESP32-C3 / ESP-IDF)

Wristband firmware for the classroom quiz. Full project overview and flashing
instructions: the repo-root [`README.md`](../README.md). BLE protocol:
[`../docs/INVOKE_BLE_ESPECIFICACAO.md`](../docs/INVOKE_BLE_ESPECIFICACAO.md).

## What it does

- **BLE** (`main/invoke_ble.c`): Nordic UART Service over GATT (the teacher app
  connects to one band = the "proxy") + BLE 5 extended advertising to fan the
  question out to the other bands and collect their answers.
- **Round state machine** (`main/invoke_round.c`): `WAIT → COUNTDOWN → ANSWER →
  RESULT`. During ANSWER the option the wrist is tilted toward (up=A, down=B,
  left=C, right=D; `main/orientation.c` roll/pitch) is highlighted live and
  latched when the timer expires.
- **Screens** (`main/screens.c`): the three TFT screens over the ST7735 driver
  (`main/st7735.c`, now a full printable-ASCII 5x7 font + word-wrap).

## Build

```sh
idf.py set-target esp32c3
idf.py -p PORT flash monitor
```

`sdkconfig.defaults` turns on NimBLE extended advertising
(`CONFIG_BT_NIMBLE_EXT_ADV`, 2 instances) — needed for the chunked question
frames. `menuconfig` → "INVOKE Band Config" sets the default band number.

## Status

The extended-advertising path is new and **not yet verified on hardware** —
tilt thresholds (`TILT_*_DEG` in `invoke_round.c`), the idle-scan duty cycle
(`scan_apply()` in `invoke_ble.c`), and the per-band countdown skew all need
tuning on real bands.
