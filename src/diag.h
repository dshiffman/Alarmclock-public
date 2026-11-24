#pragma once
#include <stddef.h>

// Hardware check for first power-up. Fills `out` with one line per item: I2C devices
// found, temperature sensor, light sensor, WiFi, memory.
// Shown on the diagnostics screen (hold a finger on the clock for 3 s), printed to the
// serial port at boot, and again whenever you send "d" over serial.
void diag_report(char *out, size_t len);
void diag_print();
void diag_serial_loop();  // watches the serial port for "d"
