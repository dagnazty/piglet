#pragma once
#include <Arduino.h>

// Heading smoothing constants
extern const uint32_t HEADING_HOLD_MS;
extern const float    HEADING_MIN_SPEED_KMPH;

// Held-heading state (used by Display)
extern double   lastGoodHeadingDeg;
extern uint32_t lastGoodHeadingMs;

// Heading smoothing
void   headingFeed(double deg);
double headingSmoothedDeg();

// Time helpers
String iso8601NowUTC();
time_t makeUtcEpochFromTm(struct tm* t);

// Baud autodetect — sniff the GPS UART at common rates and return the first
// one producing valid-looking NMEA. Tries `preferred` first to avoid
// disturbing a working configuration. Returns `preferred` if nothing detects
// (e.g. GPS module not powered yet) so the caller falls back to the cfg value.
uint32_t gpsAutodetectBaud(uint32_t preferred, int rxPin, int txPin);
