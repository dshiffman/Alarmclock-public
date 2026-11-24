# First power-up checklist

Bring the clock up one piece at a time, so that if something doesn't work there's only
one new thing to suspect. Stages 1 and 2 need only the display board and the sensors.
There's no battery backup, so stage 3 tests what happens after a power cut instead.

At every stage the **hardware check** screen tells you what the clock can see: hold a
finger on the clock screen for 3 seconds. The same report is printed to the serial
monitor (115200 baud) at boot, and again whenever you type `d` there.


If something fails, send me the serial log (or a photo of the hardware check screen).

## 0. Before the parts arrive

- [ ] Install VS Code with the PlatformIO extension (or the `pio` command line).
- [ ] Clone the repo, check out this branch, and build once: `pio run -e fnk0115q`.
      The first build downloads the toolchain and takes a few minutes.
      On Windows, if it fails with `FileNotFoundError` on a very long path, turn on long
      paths (PowerShell as administrator:
      `New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force`),
      restart VS Code and build again.
- [x] Print the fit-test frame (`case/stl/fit.stl`, about 2 1/2 hours at 0.2 mm in PETG on the Creator Pro). First print, 21 Nov: top holes and top edge 1/16" too low, and the bottom connectors' plugs stick out 1/8" below the board. Side-to-side hole spacing also 1/16" short. Case fixed (board 137.2 x 83.8 mm, 4.5 mm room under it).
- [x] Print the fixed fit test (`case/stl/fit_quick.stl`, about 1 1/2 hours) and check all four screws drop straight in. Worked (21 Nov).

## 1. Display board on USB only

Nothing connected except the USB-C data cable to your computer.

- [ ] Flash and watch the log: `pio run -e fnk0115q -t upload -t monitor`.
      If the upload doesn't start: hold **BOOT**, tap **RESET**, let go of BOOT, and try again.
- [ ] The log ends with a `---- diagnostics ----` block. The I2C line should list
      **touch (GT911)**. "Temp sensor: NOT FOUND" is expected at this stage.
- [ ] The screen shows the WiFi setup message. On your phone, join the
      **AlarmClock-Setup** network, pick your WiFi, and save. Within a minute the time,
      weather and sunrise/sunset appear.
- [ ] Hold a finger on the screen for 3 s to open the hardware check:
  - [ ] Drag a finger to all four corners. The dot should stay under your finger and
        the numbers should run from about 0,0 (top left) to 799,479 (bottom right).
  - [ ] **Play sound**: chimes from the speaker. **Stop** ends them.
  - [ ] **Light 5% / 50% / 100%**: the backlight changes smoothly.
  - [ ] **Close**.
- [ ] Double-tap the clock: the Half Moon Bay tides appear. Tap to close.
- [x] Tide screen, touch to read (added 21 Nov, works): drag a finger along the chart. A line and dot follow it and the top lines read e.g. "Tue 9:00 PM · 0.7 ft" and "Rising · high 4.1 ft at 2:06a". Lift: back to now, screen stays open. A quick tap still closes it.
- [ ] Tap the bell, add an alarm for 2 minutes from now, and save. When it rings, tap
      **SNOOZE**. The top line then reads "Snoozed · Turn off". Tap that to cancel it.
      Set another one and hold **Hold to stop** to end it.
- [ ] Check the screen window against the fit-test frame: the four screw holes and the
      visible screen area should line up. If they don't, tell me how far off they are.

## 2. Sensors (power the board off while wiring)

**SHT31** (temperature and humidity): VIN to **P6 3V3**, GND to **P6 GND**, SDA to
**P4 IO19**, SCL to **P4 IO20**. Leave ADR unconnected.

- [x] Hardware check: the I2C line shows **0x44 temp/humidity**, and "Temp sensor:
      SHT3x" shows a sensible room temperature. (13 Nov, 4:50 PM: works.)
- [x] Breathe on the sensor: humidity jumps up, then settles back within a minute. (13 Nov: temperature and humidity both rose.)
- [x] The main screen shows the inside temperature and humidity.

**BH1750** (light): VCC to **P5 3V3**, GND to **P5 GND**, SDA and SCL spliced onto the
SHT31's SDA and SCL wires. Leave ADDR unconnected.

- [x] Hardware check: the I2C line shows **0x23 light**, and the lux number goes up
      under a lamp and down when you cover the sensor. (13 Nov, 4:50 PM: works.)
- [x] Close the hardware check and cover the sensor with your thumb (or turn the room
      lights off). After about 20 s the clock goes into night mode. Uncover it: the
      screen comes back within about 2 s. (13 Nov, 4:54 PM: works.)
- [x] In the case (22 Nov): a **loose ground on the BH1750** made it read 0 lux in a lit
      room and throw out lone 19,000-lux readings that flashed the screen. Re-soldered: about
      3 lux at the desk in the evening, about 50-60 lux in lamp light, sensor in its holder under
      the hole. If the clock dims in a lit room again, check the lux on the status page
      (`/api/state`) first: a steady 0 means the wiring, not the settings. The firmware now
      ignores all-ones readings and restarts the I2C bus by itself if it jams.

Bench wiring notes (13 Nov): the board came with only two 4-pin cables, so each sensor was
tested on its own on P4 (SDA/SCL) and P6 (power). On the cables that came with the board **red is GND and black is
3V3** (backwards from the usual colours), yellow is IO19 (SDA), green is IO20 (SCL). P5 and P6
are identical (GND, 3V3, IO17, IO18): only power is usable, IO17/IO18 are the speaker amp's.
The finished clock needs two cables (P4 SDA/SCL, P6 power) plus a small
perfboard hub that splits 3V3/GND/SDA/SCL to the SHT31, BH1750 and later the SCD41. Measure
the connector pitch before ordering cables: Freenove doesn't say; it looks like 1.25-1.5 mm.

Hub layout (20 Nov): [hub-perfboard.png](hub-perfboard.png), rows J to U of the 3 x 7 cm
perfboard, 6 holes wide (about 16 x 31 mm; the outer columns and row U are a bare rim for the
case rails). Two 2x4 headers carry four buses (GND, 3V3, SCL, SDA): one input row and three
sensor outputs (SHT31, BH1750, SCD41), plus the LED strip section for later: a 2N7002 level shifter (1k
pull-up to 5 V), 680 Ω into the strip's DIN, and 1000 µF across the strip's 5 V. Wire lengths
from the board: SHT31 20 cm (the pod rides on the back cover), SCD41 15 cm, BH1750 15 cm.

CO2 holder (20 Nov): the SCD41 board (Teyleten, measured 21.74 x 13.17 mm, 7.62 mm over the
sensor) does not get its own pod. It slides into a holder on the inside of the back cover,
sensor facing a patch of four vents with a 0.8 mm air gap, between the speaker and the USB
socket, about 80 mm from the SHT31 pod so its warmth doesn't reach it. Pins end toward the
middle; solder wires straight into the pin holes. Two flexing latch arms click over the pin end (no glue). Picture: [co2-holder.png](co2-holder.png).
Hub holder (20 Nov): The hub was trimmed to 5 columns: 31.85 x 14.75 mm. It mounts on the inside of the back
cover in the middle (not on the bottom wall: the shell is only about 31 mm deep behind the board),
3.5 mm off the cover so the solder and wires underneath are clear, with the same rails and snap
latch as the CO2 sensor. Plugs reach about 20 mm off the cover; the board's back is about 34 mm
away. Picture: [hub-holder.png](hub-holder.png). Board 1.5 mm thick (measured).
To check when printed: the board slides in and stays put; CO2 settles near 420 ppm next to an
open window.

Hub on the bench (20 Nov): soldered the four sensor rows (U-R) and plugged all
three sensors in. Hardware check: I2C finds 0x23 BH1750, 0x44 SHT3x, 0x5D touch and 0x62 SCD41.
Readings: 22.7 C / 72.9 F, 48 %RH, 3.1 lux, CO2 679 ppm (SCD41 serial read OK). WiFi up, time
set, heap 77 KB free (lowest 24). **First time all three sensors ran together: the hub works.**
LED-strip part of the hub not built yet.

Display noise (19 Nov): constant noise and lines on every screen. First seen while the
wire unsoldered from the 3.3 V regulator; reheating the joint and a power cycle cleared it.
It came back about 10 minutes later with nobody touching the board. The clock kept running
normally underneath (web page answered, up 9 min), and a processor-only restart cleared it, so
it's the processor's picture output losing sync, not the panel itself. Cause still open: a
marginal 3.3 V joint dipping the supply, or the screen output being starved under load (a known
weak spot on the ESP32-S3; the earlier picture shift was related). If it comes back: press
RESET (not unplug) to confirm a restart clears it, and note what was happening.

20 Nov, 11:29 AM: back again on the clock face, nobody touching it. Photos: short scraps of
edges (under the digits, beside INSIDE's dashes) that flicker in and out; RESET cleared it. That
is the screen's line buffer falling behind (it copies the picture out of PSRAM; when PSRAM is
busy the panel shows stale lines). The chip already re-syncs every frame. Found and fixed: three
labels were being redrawn 4 times a second for nothing, keeping PSRAM busy; idle redraws are
now zero. Watching to see if it returns. If it does: before pressing RESET, read `drawRate` from
http://alarmclock.local/api/state (screens redrawn per minute; near 0 when idle), or type `f` in
the serial monitor to list what's being redrawn. Next step after that would be rebuilding the
framework with the RGB interrupt kept in internal RAM (CONFIG_LCD_RGB_ISR_IRAM_SAFE), so flash
writes (the alarm state is saved once a minute) can't stall the screen output.

At this point all the hardware is tested.

## 3. Power cuts (no battery)

A battery backup was tried and dropped: a small charger and boost module couldn't supply the
board's start-up surge, so the board kept browning out and restarting. Instead the clock just
restarts after a power cut, gets the time from the internet and rings any alarm it missed.

What has to survive a restart, and how to check it:

- [ ] Set an alarm and change a setting (say the night hours), unplug for 10 s, plug back in.
      Both are still there (they're saved in flash).
- [ ] After plugging back in, the clock shows "--:--" and "Connecting to WiFi..." or
      "Getting the time...", then the right time within a few seconds of WiFi connecting.
- [ ] **Missed alarm:** set an alarm 2 minutes ahead, unplug before it rings, plug back in
      about 3 minutes later. It rings as soon as the time is back. The serial log says
      "came due while the clock was off; ringing now". (It only rings if it's no more than
      30 minutes late.)
- [ ] **Snooze across a cut:** let an alarm ring, snooze it, unplug for a minute, plug back
      in. It still says "Snoozed" and rings when the snooze runs out. Unplug past the end of
      a snooze and it rings as soon as the time is back.
- [ ] **Ringing across a cut:** unplug while an alarm is ringing, plug back in: it rings
      again (stopping 30 minutes after it first started).

## 3b. Sleep sounds

- [ ] Tap the music note left of the alarm, pick Rain, press Play. It fades in over a couple
      of seconds. Try Ocean, the volume buttons, and Stop (fades out).
- [ ] Set "15 min" and leave it: the note shows the minutes left, and it fades to silence
      over the last 2 minutes.
- [ ] With rain playing, let an alarm ring: the rain stops and the alarm takes over.
- [ ] On the phone page, the Sleep sounds card shows the same state and can start and stop it.

## 3c. Sunrise

- [ ] Open an alarm, press **Preview** next to Sunrise. The screen should go black, then deep
      red, orange and warm white over about a minute, with the backlight rising, then hold
      for 10 s and go back to the editor. Tap during it: it ends at once.
- [ ] Best in a dark room: is the start dark enough (no sudden jump), and is the end bright
      enough to wake to? Note anything that steps or flickers.
- [ ] Real one: set an alarm a few minutes ahead with Sunrise 15 min (it starts at once if the
      alarm is less than 15 min away). The clock face says nothing; the screen just starts
      to glow. When the alarm rings, the ringing screen takes over.
- [ ] Phone page: the alarm editor has the Sunrise setting and its Preview button starts one
      on the clock. The alarm list shows "sunrise 20 min".
- [ ] Alarms set before this update kept their settings (sunrise off).

## 4. Case

- [ ] Print the shell, back cover and pod (see the README for settings and assembly).
      USB-C cable measured 20 Nov: socket screws 16.9 mm apart (`usb_screw_dx`), and the
      right-angle plug sticks out 9 mm past the board's edge (`usb_room` now 9.5 mm each
      side, so the case is about 161 mm wide).
- [ ] Slide the BH1750 face up into the rails under the slot in the top. Screw the USB-C
      socket into the back cover and plug the right-angle end into the board's USB-C port.
- [ ] Flash the firmware once through the back socket, to be sure it carries data.
- [ ] Print the base, fill its pocket with about 150 g of stick-on steel wheel weights, add
      rubber feet, and tape the clock into it. Tap hard at the top of the screen: it shouldn't rock.
- [ ] Close it up and run the hardware check once more.

## Decide once the hardware is tested

- [ ] **Catch-up window.** A missed alarm rings if it's up to 30 minutes late
      (`kCatchUpMinutes` in `src/alarm.cpp`). If the router takes longer than that to come
      back after a cut, the clock has no time and can't ring. Decide whether 30 minutes is
      right once a real outage has been timed.

The battery questions that used to be here (cutoff, IO13 sense, battery % scale) went with
the battery on 19 Nov.
