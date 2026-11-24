# Bedside alarm clock

An original bedside clock (not a copy of any product) with inside temperature and humidity,
optional CO2, outside weather, sunrise/sunset, NOAA tides, alarms with snooze, sleep sounds, a
screen sunrise and a dim night mode. No battery: after a power cut it restarts, gets the time
from the internet and rings any alarm it missed (up to 30 min late). See README.md for the
feature list and assembly, and docs/bring-up.md for the hardware bring-up checklist.

## Hardware

- Freenove ESP32-S3 Display **FNK0115Q** (5" 800x480 IPS, GT911 touch, NS4168 amp + speaker,
  CH340 USB-serial). Schematic: github.com/Freenove/Freenove_ESP32_S3_Display_FNK0115.
- SHT31 temp/humidity (SHT4x also supported), BH1750 light sensor and SCD4x CO2 sensor on I2C
  (SDA IO19, SCL IO20), shared with the touch chip. The bus runs at 100 kHz: 400 kHz broke
  touch once the sensor hub's longer wires were on it.
- Power: USB-C supply -> panel-mount socket in the back cover -> flat right-angle USB-C
  cable -> board.
- Pins are all in `src/board.h`. Only IO11, IO12 and IO13 (header P3) are spare; IO11 and IO13
  have SD-slot pull-ups. IO9 is a display data line and IO17/IO18 are audio: never use them
  for anything else.

## Build, flash, monitor

```
pio run -e fnk0115q                      # build
pio run -e fnk0115q -t upload -t monitor # flash and watch the serial log (115200)
pio run -e ota -t upload                 # flash over WiFi (add --upload-port <IP> if needed)
```

Set your own WiFi-update password in `platformio.ini` (`[ota] password`) before the first
USB upload. On Windows, if unpacking the ESP32 packages fails on a long path, turn on long
paths. If no COM port appears, install the CH340 driver. If the upload doesn't start: hold
BOOT, tap RESET, release BOOT, retry.

In the serial monitor, `d` prints the hardware diagnostics report and `t` logs touches for
15 s. On the clock, a 3-second hold on the screen opens the same report.

## Working here

- Keep the clock simple to read: no seconds, whole-degree inside temperature,
  plain-language text on screen and in docs.
- The web page in `web/` is embedded into the firmware by `tools/embed_web.py` on each build.
- Diagrams in docs/ come from the scripts in tools/diagrams/; edit the script, then re-render the PNG.
- The case is `case/clock_case.scad` (OpenSCAD). Every part must print without supports.
