#pragma once
#define FIRMWARE_VERSION "v2.4"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPSPlus.h>
#include "PinMapDefs.h"
#include "Config.h"

// OLED display dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ---- Global objects ----
extern PinMap pins;
extern Config cfg;
extern Adafruit_SSD1306 display;
extern TinyGPSPlus gps;
extern HardwareSerial GPSSerial;
extern WebServer server;

// ---- State flags ----
extern bool sdOk;
extern bool scanningEnabled;
extern bool gpsHasFix;
extern bool allowScanForOled;
extern bool userScanOverride;
extern bool autoPaused;

// ---- GPS time-source tracking ----
// Source of the timestamp embedded in the most recent CSV row.
//   0 = GPS    — fresh GPS date+time (within cfg.gpsFixAgeMaxMs)
//   1 = SYSTEM — system clock (previously disciplined from GPS)
//   2 = PLACEHOLDER — neither available; fallback "1970-01-01 00:MM:SS"
extern uint8_t  gpsTimeSource;
extern uint32_t gpsTimeFallbackCount;  // rows that fell back off GPS (sources 1+2)

// ---- SD space accounting ----
extern uint64_t sdFreeBytes;
extern uint64_t sdTotalBytes;
extern bool     sdLowSpace;     // < SD_LOW_SPACE_BYTES free
extern bool     sdCritical;     // < SD_CRITICAL_BYTES free  -> stop logging
// Thresholds (raise/lower if needed; chosen to leave room for at least
// a few minutes of further logging once "LOW" trips on a typical card).
static const uint64_t SD_LOW_SPACE_BYTES = 50ULL * 1024ULL * 1024ULL;  // 50 MB
static const uint64_t SD_CRITICAL_BYTES  =  5ULL * 1024ULL * 1024ULL;  //  5 MB

// ---- OLED page system ----
// 0=Status, 1=Networks, 2=Navigation, 3=Pause, 4=Pig, 5=MeshNode
static const uint8_t PAGE_COUNT = 6;
extern uint8_t currentPage;
extern bool statusPagePaused;   // true when user double-pressed to pause on status page

// ---- AP state ----
extern uint32_t apStartMs;
extern bool apClientSeen;
extern bool apWindowActive;
extern const uint32_t AP_WINDOW_MS;

// Extended AP window: triggered when the first client connects during the initial
// 60 s window. Replaces the 60 s budget with a 5 min rolling timer that the WebUI
// can reset via /extend, and the user can short-circuit via /start.
extern bool apExtended;
extern uint32_t apExtendedStartMs;
extern bool apForceClose;
extern const uint32_t AP_EXTENDED_WINDOW_MS;
extern const uint32_t AP_EXTEND_PROMPT_LEAD_MS;

// ---- Counters ----
extern uint32_t networksFound2G;
extern uint32_t networksFound5G;

// ---- Log state ----
extern File logFile;
extern String currentCsvPath;
extern wl_status_t lastStaStatus;

// ---- Upload state ----
extern bool     uploading;
extern bool     uploadPausedScanWasEnabled;
extern uint32_t uploadTotalFiles;
extern uint32_t uploadDoneFiles;
extern String   uploadCurrentFile;
extern String   uploadLastResult;
extern String   uploadTargetName;   // service label shown on OLED (e.g. "WDGW UL" / "WiGLE UL")
extern uint32_t uploadFailedFiles;  // failed upload attempts in the current batch
extern int      wigleTokenStatus;
extern int      wigleLastHttpCode;

// ---- WiGLE constants ----
extern const char* WIGLE_HOST;
extern const uint16_t WIGLE_PORT;

// ---- Optional TLS buffer sizing (no-op for broad core compatibility) ----
inline void tlsMaybeSetBufferSizes(WiFiClientSecure&, int, int) {}
