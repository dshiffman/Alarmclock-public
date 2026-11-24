#pragma once
#include <Arduino.h>

// WiFi setup (captive portal on first boot), network time, and mDNS name.

enum class NetState {
  Connecting,    // trying saved WiFi
  SetupPortal,   // no WiFi saved / can't connect: phone setup network is up
  Online,
};

static constexpr const char *kSetupApName = "AlarmClock-Setup";
static constexpr const char *kHostname = "alarmclock";

void net_init();
void net_loop();

NetState net_state();
bool net_time_valid();  // true once the clock has been set from the internet
String net_ip();

// Call when settings that affect time (time zone) change.
void net_apply_timezone();
