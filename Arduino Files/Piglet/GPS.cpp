#include "GPS.h"
#include "Globals.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>

// ---- Heading smoothing ----

static const uint8_t HEADING_AVG_N = 8; // samples in moving window (tune 5-12)
const uint32_t HEADING_HOLD_MS = 20000UL;    // keep last good heading for 20s if GPS drops
const float    HEADING_MIN_SPEED_KMPH = 3.0f; // below this, heading is usually noisy/unreliable

static float headingSinBuf[8] = {0};
static float headingCosBuf[8] = {0};
static uint8_t headingIdx = 0;
static uint8_t headingCount = 0;
static float headingSinSum = 0.0f;
static float headingCosSum = 0.0f;

double   lastGoodHeadingDeg = NAN;
uint32_t lastGoodHeadingMs  = 0;

static inline double normDeg360(double deg) {
  while (deg < 0) deg += 360.0;
  while (deg >= 360.0) deg -= 360.0;
  return deg;
}

// Feed a new heading sample (deg). Updates circular moving average sums.
void headingFeed(double deg) {
  deg = normDeg360(deg);
  const float r = (float)(deg * (M_PI / 180.0));
  const float s = sinf(r);
  const float c = cosf(r);

  if (headingCount < HEADING_AVG_N) {
    headingCount++;
  } else {
    // remove outgoing sample from sums
    headingSinSum -= headingSinBuf[headingIdx];
    headingCosSum -= headingCosBuf[headingIdx];
  }

  headingSinBuf[headingIdx] = s;
  headingCosBuf[headingIdx] = c;
  headingSinSum += s;
  headingCosSum += c;

  headingIdx++;
  if (headingIdx >= HEADING_AVG_N) headingIdx = 0;
}

// Return smoothed heading in degrees; NAN if no samples yet
double headingSmoothedDeg() {
  if (headingCount == 0) return NAN;
  // atan2(y, x) where y=sin sum, x=cos sum
  double ang = atan2((double)headingSinSum, (double)headingCosSum) * (180.0 / M_PI);
  return normDeg360(ang);
}

// ---- Time helpers ----

time_t makeUtcEpochFromTm(struct tm* t) {
  // Save current TZ
  const char* oldTz = getenv("TZ");

  // Force UTC
  setenv("TZ", "UTC0", 1);
  tzset();

  time_t epoch = mktime(t);  // treated as UTC while TZ=UTC0

  // Restore TZ (or unset)
  if (oldTz) setenv("TZ", oldTz, 1);
  else unsetenv("TZ");
  tzset();

  return epoch;
}

// ---- Baud autodetect ----
//
// Two `$` characters seen in a 1.5 s window is enough — at any standard NMEA
// rate the GPS produces several sentences per second, so a baud mismatch
// yields garbage (no `$` characters or random bytes well below the
// printable-ASCII range).
static bool gpsBaudWorks(uint32_t baud, int rxPin, int txPin, uint32_t timeoutMs) {
  GPSSerial.end();
  GPSSerial.begin(baud, SERIAL_8N1, rxPin, txPin);
  uint32_t t0 = millis();
  int dollars = 0;
  while ((millis() - t0) < timeoutMs) {
    while (GPSSerial.available()) {
      char c = GPSSerial.read();
      if (c == '$') dollars++;
      if (dollars >= 2) return true;
    }
    delay(2);
  }
  return false;
}

uint32_t gpsAutodetectBaud(uint32_t preferred, int rxPin, int txPin) {
  static const uint32_t CANDIDATES[] = { 9600, 38400, 115200, 4800, 19200, 57600 };
  static const size_t N = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

  Serial.printf("[GPS] Autodetect: trying preferred %lu first...\n",
                (unsigned long)preferred);
  if (gpsBaudWorks(preferred, rxPin, txPin, 1500)) {
    Serial.printf("[GPS] Autodetect: %lu OK\n", (unsigned long)preferred);
    return preferred;
  }

  for (size_t i = 0; i < N; i++) {
    uint32_t b = CANDIDATES[i];
    if (b == preferred) continue;
    Serial.printf("[GPS] Autodetect: trying %lu...\n", (unsigned long)b);
    if (gpsBaudWorks(b, rxPin, txPin, 1200)) {
      Serial.printf("[GPS] Autodetect: %lu OK (overrides cfg %lu for this session)\n",
                    (unsigned long)b, (unsigned long)preferred);
      return b;
    }
  }

  Serial.printf("[GPS] Autodetect: no baud responded — staying on %lu (GPS may be off)\n",
                (unsigned long)preferred);
  // Restore preferred so the caller's GPSSerial state matches the returned baud.
  GPSSerial.end();
  GPSSerial.begin(preferred, SERIAL_8N1, rxPin, txPin);
  return preferred;
}

// Note the source of the most recent timestamp on transition. Logs once per
// change so a long-running drive isn't spammed, but a boot with no fix
// produces a single visible "[TIME] source -> SYSTEM" line in the log.
static void noteTimeSource(uint8_t newSource) {
  if (newSource != 0) gpsTimeFallbackCount++;
  if (newSource == gpsTimeSource) return;
  gpsTimeSource = newSource;
  const char* name = (newSource == 0) ? "GPS"
                   : (newSource == 1) ? "SYSTEM (no fresh GPS time — drift possible)"
                   :                    "PLACEHOLDER (no GPS, no system clock — rows will read 1970)";
  Serial.printf("[TIME] source -> %s\n", name);
}

String iso8601NowUTC() {
  uint32_t maxAge = cfg.gpsFixAgeMaxMs;
  if (maxAge < 1000) maxAge = 1000;  // floor; cfg validation also clamps

  // 1) Prefer GPS UTC time when fresh/valid
  if (gps.date.isValid() && gps.time.isValid() &&
      gps.date.age() < maxAge && gps.time.age() < maxAge) {

    noteTimeSource(0);
    char buf[32];
    snprintf(buf, sizeof(buf),
             "%04d-%02d-%02d %02d:%02d:%02d",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
    return String(buf);
  }

  // 2) If system time is set (from GPS in your loop), use it
  time_t now = time(nullptr);
  if (now > 1700000000) { // sanity check (~2023+)
    noteTimeSource(1);
    struct tm tmUtc;
    gmtime_r(&now, &tmUtc);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmUtc);
    return String(buf);
  }

  // 3) Last resort: millis-based placeholder
  noteTimeSource(2);
  uint32_t s = millis() / 1000;
  char buf[32];
  snprintf(buf, sizeof(buf), "1970-01-01 00:%02lu:%02lu",
           (unsigned long)((s/60)%60), (unsigned long)(s%60));
  return String(buf);
}
