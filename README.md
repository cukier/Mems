# LSM6DS3 reader (ESP32-C3 / ESP-IDF)

Reads accelerometer + gyroscope from an LSM6DS3 breakout over I2C and logs
values once every 200 ms.

Board: ESP32-C3 Super Mini variant (RISC-V) with integrated 1.44" display and
case — it only breaks out GPIO0-10 plus GPIO20/GPIO21, so wiring uses those.

## Wiring

| LSM6DS3 pin | ESP32-C3 pin    |
|-------------|-----------------|
| VCC         | 3V3             |
| GND         | GND             |
| SCL         | GPIO20          |
| SDA         | GPIO21          |
| SA0         | leave floating/pulled high -> I2C address 0x6B (see `lsm6ds3.h` to switch to 0x6A) |

GPIO20/21 double as UART0 RX/TX on this chip. That's fine here: ESP-IDF's
console/flashing on this board goes over the native USB (USB-Serial-JTAG),
not UART0, so those pins are free for I2C. If your build instead logs over
UART0, move SDA/SCL to two of the free GPIO0-10 pins.

Pull-ups on SDA/SCL are enabled in software (`i2c_config_t.sda_pullup_en` /
`scl_pullup_en`); add external 4.7k pull-ups to 3V3 if your breakout doesn't
already have them.

## Build

```
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
