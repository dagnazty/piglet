# Changelog

## v2.4 (2026-05-02)

Reliability pass focused on **never silently losing data** and **never silently
lying about timestamps**. All three variants (XIAO Piglet, T-Dongle C5, Waveshare
Mini) ported in lockstep.

### SD reliability (Tier 1)
- **Free-space accounting**: tracks free/total bytes; OLED & WebUI now show
  `OK / LOW / FULL / FAIL` instead of just `OK / FAIL`. Defaults: LOW < 50 MB,
  CRITICAL < 5 MB.
- **Refuse to open new logs when CRITICAL** so a near-full card can't corrupt
  an existing file when clusters run out mid-row.
- **Mid-drive recheck** every ~200 rows; if it crosses CRITICAL the active
  log is flushed and closed cleanly (uploads/WebUI keep working).
- **Write-failure detection**: 3 consecutive `println()` failures marks SD
  bad and trips `FAIL` on the display so a yanked or stuck card is obvious.
- **Adaptive flush batching**: drops 25→10 lines after a slow flush (>500 ms)
  and recovers when the card catches up.
- **Verified close before deep sleep** (XIAO): post-close stat confirms the
  file exists with non-zero size; otherwise the OLED shows
  `SD WARN — Last write may not have saved` for 2 s before powering off.

### GPS / time correctness (Tier 2)
- **`gpsFixAgeMaxMs` is now configurable** (range 1000..600000). Default
  unchanged per board (XIAO 15 s, T-Dongle/Waveshare 5 s).
- **Time-source tracking**: every CSV row now records which clock produced
  its timestamp — `GPS` (good), `SYSTEM` (drift possible), or `PLACEHOLDER`
  (1970 fallback). Logged once per transition (`[TIME] source -> SYSTEM`).
- **Visible in WebUI**: GPS pill now shows `LOCK (sys time)` in amber or
  `NO FIX (no time!)` in red when rows aren't using fresh GPS time. Fallback
  counter exposed in `/status.json`.
- **GPS baud autodetect** (`gpsAutodetect=true` by default): probes
  9600/38400/115200/4800/19200/57600 if the configured baud isn't producing
  NMEA. Detected baud overrides for the session only — never written back to
  `/wardriver.cfg`, so a transient GPS issue can't quietly edit user config.
- **`gpsBaud` whitelist**: typos like `gpsBaud=9650` now log a warning
  instead of silently bricking the GPS UART.

### Notes
- All new config keys ship with sane defaults; **existing `/wardriver.cfg`
  files keep working unchanged**.
- Detected GPS baud is intentionally **not** persisted — set `gpsBaud`
  explicitly if you want to skip the ~1.5 s autodetect probe at boot.

---

## v1.3-beta (2026-02-23)

### New Features
- **WiGLE Upload History Tracking**: Web UI now displays upload statistics (new networks discovered, total networks) for uploaded files
- **Automatic Boot Upload with Quota Management**: Configurable `maxBootUploads` setting (default: 25) to control how many files upload automatically at boot
- **24-Hour History Caching**: Upload history API calls are cached for 24 hours to conserve WiGLE API quota (25 calls/day limit)
- **On-Demand History Refresh**: History automatically refreshes in web UI when cache expires (only when connected to home network)

### Improvements
- **Optimized Upload Performance**: Removed token pre-checks and reduced timeouts for faster batch uploads
- **Enhanced WiFi Stability**: Scanning now properly pauses when connected to home network to prevent connection drops
- **Web Server Startup Timing**: Web server now starts after WiGLE operations complete to avoid resource conflicts
- **Improved Configuration Management**: Added `maxBootUploads` and `speedUnits` configuration options
- **Better Status Display**: Config form in web UI now properly displays all saved values including WiGLE token

### Bug Fixes
- Fixed scanning interference causing 100% ping loss when connected to home WiFi
- Fixed web UI configuration display issues (all fields now populate correctly)
- Fixed chunked encoding errors in `/status.json` and `/files.json` endpoints
- Corrected WiGLE token display (now shows actual token instead of "(set)")
- Fixed JSON buffer overflow issues in files endpoint

### Technical Changes
- Increased JSON buffer for files endpoint from 4KB to 8KB to handle upload statistics
- Switched from HTTP/1.1 to HTTP/1.0 for WiGLE API compatibility
- Added proper `client.flush()` to ensure complete data transmission
- Reduced upload timeout from 60s to 25s for better reliability
- History parsing now uses incremental JSON parsing to reduce memory fragmentation

### Configuration
- New config option: `maxBootUploads` - Max CSV files to upload at boot (0-25, default: 25)
- Updated config option: `speedUnits` - Display speed in km/h or mph
- Config file now saves `maxBootUploads` setting to `/wardriver.cfg`

### Requirements
- **CRITICAL**: PSRAM must be enabled in Arduino IDE for reliable TLS/HTTPS uploads
  - ESP32-C5/C6: Use OPI PSRAM
  - ESP32-S3: Use QSPI PSRAM
- Arduino-ESP32 core v3.0.0 or later
- Updated library dependencies documented in README

### Known Issues
- ESP32-C5/C6 require PSRAM enabled or TLS connections will fail due to insufficient heap
- Initial boot may show "Failed to allocate dummy cacheline for PSRAM" warning (can be ignored)

### Migration Notes
- No breaking changes from v1.2
- Existing `/wardriver.cfg` files are compatible
- New `maxBootUploads` setting will default to 25 if not present in config

---

## v1.2 (Previous Release)
- Initial stable release with basic wardriving functionality
- SD card CSV logging
- Web UI for file management
- Manual WiGLE upload support
