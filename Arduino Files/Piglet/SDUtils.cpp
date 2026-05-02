#include "SDUtils.h"
#include "Globals.h"

// ---- Path helpers ----

String pathBasename(const String& p) {
  int slash = p.lastIndexOf('/');
  if (slash < 0) return p;
  return p.substring(slash + 1);
}

String normalizeSdPath(const char* dir, const char* nameIn) {
  if (!dir || !nameIn) return "";

  String d(dir);
  String n(nameIn);

  d.trim();
  n.trim();

  if (d.length() == 0 || n.length() == 0) return "";

  // Ensure dir starts with "/"
  if (d[0] != '/') d = "/" + d;

  // Strip trailing "/" from dir
  while (d.endsWith("/")) d.remove(d.length() - 1);

  // Case A: name is already absolute: "/logs/foo.csv" or "/uploaded/foo.csv"
  if (n[0] == '/') {
    // If SD lib already gives full path, just return it
    return n;
  }

  // Case B: name is "logs/foo.csv" (no leading slash)
  // If it starts with the same directory name, convert to absolute.
  // Example: dir="/logs", name="logs/foo.csv" => "/logs/foo.csv"
  String dNoSlash = d;
  if (dNoSlash.startsWith("/")) dNoSlash = dNoSlash.substring(1); // "logs"

  if (n.startsWith(dNoSlash + "/")) {
    return "/" + n;  // make it absolute
  }

  // Case C: name is just "foo.csv"
  // Join dir + "/" + name
  return d + "/" + n;
}

bool isAllowedDataPath(const String& p) {
  return p.startsWith("/logs/") || p.startsWith("/uploaded/");
}

// ---- Move to uploaded ----

bool moveToUploaded(const String& srcPath) {
  if (!sdOk) return false;
  if (!SD.exists(srcPath)) {
    Serial.print("[SD] moveToUploaded: source missing: ");
    Serial.println(srcPath);
    return false;
  }

  // Ensure folder exists
  if (!SD.exists("/uploaded")) {
    Serial.println("[SD] Creating /uploaded ...");
    if (!SD.mkdir("/uploaded")) {
      Serial.println("[SD] ERROR: SD.mkdir(/uploaded) failed");
      return false;
    }
  }

  String dstPath = String("/uploaded/") + pathBasename(srcPath);

  // If destination exists, remove it first (rename may fail otherwise)
  if (SD.exists(dstPath)) {
    Serial.print("[SD] Removing existing dst: ");
    Serial.println(dstPath);
    SD.remove(dstPath);
  }

  Serial.print("[SD] Moving ");
  Serial.print(srcPath);
  Serial.print(" -> ");
  Serial.println(dstPath);

  bool ok = SD.rename(srcPath, dstPath);
  if (!ok) {
    Serial.println("[SD] ERROR: SD.rename failed");
    // Last resort: copy + delete (some SD libs are picky)
    File in = SD.open(srcPath, FILE_READ);
    if (!in) { Serial.println("[SD] copy fallback: open src failed"); return false; }

    File out = SD.open(dstPath, FILE_WRITE);
    if (!out) { Serial.println("[SD] copy fallback: open dst failed"); in.close(); return false; }

    uint8_t buf[1024];
    while (true) {
      int n = in.read(buf, sizeof(buf));
      if (n <= 0) break;
      out.write(buf, n);
      delay(0);
    }
    out.flush();
    out.close();
    in.close();

    // Verify copy
    if (!SD.exists(dstPath)) {
      Serial.println("[SD] copy fallback: dst does not exist after write");
      return false;
    }

    if (!SD.remove(srcPath)) {
      Serial.println("[SD] copy fallback: WARNING failed to remove src after copy");
      // still consider it moved-ish, but warn
    }

    Serial.println("[SD] copy fallback: OK");
    return true;
  }

  Serial.println("[SD] Move OK");
  return true;
}

// ---- SD space accounting ----

void updateSdSpaceInfo() {
  if (!sdOk) {
    sdFreeBytes  = 0;
    sdTotalBytes = 0;
    sdLowSpace   = false;
    sdCritical   = false;
    return;
  }
  uint64_t total = SD.totalBytes();
  uint64_t used  = SD.usedBytes();
  uint64_t freeB = (total > used) ? (total - used) : 0;

  sdTotalBytes = total;
  sdFreeBytes  = freeB;
  sdLowSpace   = (freeB <  SD_LOW_SPACE_BYTES);
  sdCritical   = (freeB <  SD_CRITICAL_BYTES);

  Serial.printf("[SD] Space: free=%llu MB / total=%llu MB%s%s\n",
                (unsigned long long)(freeB / (1024ULL * 1024ULL)),
                (unsigned long long)(total / (1024ULL * 1024ULL)),
                sdLowSpace ? "  [LOW]"      : "",
                sdCritical ? "  [CRITICAL]" : "");
}

// ---- Log file ----

// Sanitise a user-provided device name for safe use in filenames.
// Keeps alphanumerics, hyphens, underscores; replaces spaces with _;
// strips everything else; truncates to 20 chars.
static String sanitiseDeviceName(const String& raw) {
  String s = raw;
  s.replace(" ", "_");
  for (int i = (int)s.length() - 1; i >= 0; i--) {
    char c = s[i];
    if (!isAlphaNumeric(c) && c != '_' && c != '-') s.remove(i, 1);
  }
  if (s.length() > 20) s = s.substring(0, 20);
  return s;
}

static String newCsvFilename() {
  if (!SD.exists("/logs")) SD.mkdir("/logs");

  // Build optional prefix:  "name_Piglet_"  or empty
  String prefix = "";
  if (cfg.deviceName.length() > 0) {
    String safe = sanitiseDeviceName(cfg.deviceName);
    if (safe.length() > 0) prefix = safe + "_Piglet_";
  }

  // Make collisions extremely unlikely: millis + esp_random
  for (int tries = 0; tries < 25; tries++) {
    uint32_t r = (uint32_t)esp_random();
    char buf[100];
    snprintf(buf, sizeof(buf), "/logs/%sWiGLE_%lu_%08lX.csv",
             prefix.c_str(), (unsigned long)millis(), (unsigned long)r);
    String p(buf);
    if (!SD.exists(p)) return p;
  }

  // last-resort fallback
  char buf2[100];
  snprintf(buf2, sizeof(buf2), "/logs/%sWiGLE_%lu.csv",
           prefix.c_str(), (unsigned long)millis());
  return String(buf2);
}

bool openLogFile() {
  if (!sdOk) return false;

  // Refresh space info so we can refuse to open if the card is full —
  // continuing would either fail-open with no rows or, worse, corrupt
  // an existing file when the FS runs out of clusters.
  updateSdSpaceInfo();
  if (sdCritical) {
    Serial.printf("[SD] REFUSING to open new log: only %llu KB free (< %llu KB critical)\n",
                  (unsigned long long)(sdFreeBytes / 1024ULL),
                  (unsigned long long)(SD_CRITICAL_BYTES / 1024ULL));
    currentCsvPath = "";
    return false;
  }

  // Close any previous handle
  closeLogFile();

  // Pick a fresh filename FIRST
  currentCsvPath = newCsvFilename();

  Serial.print("[SD] Opening log file: ");
  Serial.println(currentCsvPath);

  logFile = SD.open(currentCsvPath, FILE_WRITE);
  if (!logFile) {
    Serial.println("[SD] Failed to open log file for write");
    return false;
  }

  // Build device field: Piglet-{name} if set, otherwise Piglet-Wardriver
  String deviceField = "Piglet-Wardriver";
  if (cfg.deviceName.length() > 0) {
    String safe = sanitiseDeviceName(cfg.deviceName);
    if (safe.length() > 0) deviceField = "Piglet-" + safe;
  }
  logFile.print("WigleWifi-1.4,appRelease=1,model=Xiao-ESP32S3,release=1,device=");
  logFile.println(deviceField);
  logFile.println("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type");
  logFile.flush();

  Serial.println("[SD] Log file initialized with WiGLE headers");
  return true;
}

bool closeLogFile() {
  if (!logFile) return true;  // nothing to close, treat as fine

  Serial.println("[SD] Closing log file");
  logFile.flush();
  logFile.close();

  // Post-close verification: if the path doesn't exist or is zero bytes,
  // the card likely dropped the buffer (stuck/corrupt SD, or it was yanked).
  // We don't try to recover here — the caller (e.g. enterDeepSleep) decides
  // how to surface this.
  if (currentCsvPath.length() == 0) return false;
  if (!SD.exists(currentCsvPath)) {
    Serial.printf("[SD] WARN: post-close stat: %s does not exist\n", currentCsvPath.c_str());
    return false;
  }
  File f = SD.open(currentCsvPath, FILE_READ);
  if (!f) {
    Serial.printf("[SD] WARN: post-close re-open failed: %s\n", currentCsvPath.c_str());
    return false;
  }
  size_t sz = f.size();
  f.close();
  if (sz == 0) {
    Serial.printf("[SD] WARN: post-close size==0: %s\n", currentCsvPath.c_str());
    return false;
  }
  Serial.printf("[SD] Close verified: %s (%u bytes)\n", currentCsvPath.c_str(), (unsigned)sz);
  return true;
}

void appendWigleRow(const String& mac, const String& ssid, const String& auth,
                    const String& firstSeen, int channel, int rssi,
                    double lat, double lon, double altM, double accM) {
  if (!sdOk || !logFile) return;

  // If a previous space recheck tripped CRITICAL, stop writing rather than
  // letting the FS run out of clusters mid-row (which can corrupt the file).
  if (sdCritical) return;

  String safeSsid = ssid;
  safeSsid.replace("\"", "\"\"");

  String line;
  line.reserve(256);
  line += mac; line += ",";
  line += "\""; line += safeSsid; line += "\",";
  line += auth; line += ",";
  line += firstSeen; line += ",";
  line += String(channel); line += ",";
  line += String(rssi); line += ",";
  line += String(lat, 6); line += ",";
  line += String(lon, 6); line += ",";
  line += String(altM, 1); line += ",";
  line += String(accM, 1); line += ",";
  line += "WIFI";

  // Detect write failures: File::println returns 0 on a failed write.
  // Three in a row means the card is gone — mark sdOk=false so future
  // calls bail fast and the OLED can show "FAIL".
  static uint8_t consecutiveWriteFails = 0;
  size_t wrote = logFile.println(line);
  if (wrote == 0) {
    consecutiveWriteFails++;
    Serial.printf("[SD] write FAIL (%u consecutive)\n", consecutiveWriteFails);
    if (consecutiveWriteFails >= 3) {
      Serial.println("[SD] giving up: marking SD bad, closing log");
      logFile.close();
      sdOk = false;
      return;
    }
  } else {
    consecutiveWriteFails = 0;
  }

  // Flush less often to avoid stalls (SD writes can block hard).
  // Adapt: if a previous flush ran long, drop the line-batch to flush
  // sooner and keep total stall time per call lower.
  static uint32_t lastFlushMs    = 0;
  static uint32_t linesSinceFlush = 0;
  static uint32_t flushBatchLines = 25;  // adaptive: 10..25
  static uint32_t rowsSinceSpaceCheck = 0;

  linesSinceFlush++;
  rowsSinceSpaceCheck++;

  uint32_t nowMs = millis();
  if (linesSinceFlush >= flushBatchLines || (nowMs - lastFlushMs) >= 2000) {
    uint32_t t0 = millis();
    logFile.flush();
    uint32_t flushDur = millis() - t0;
    lastFlushMs     = nowMs;
    linesSinceFlush = 0;

    if (flushDur > 500) {
      // SD is being slow — shorten the batch so we hit fewer rows per stall.
      if (flushBatchLines > 10) flushBatchLines = 10;
      Serial.printf("[SD] slow flush %u ms — batch -> %u\n",
                    (unsigned)flushDur, (unsigned)flushBatchLines);
    } else if (flushDur < 50 && flushBatchLines < 25) {
      flushBatchLines = 25;
    }
  }

  // Periodic space recheck. Cheap relative to a full scan, but not free.
  // Every 200 rows is roughly every minute or two on a typical drive.
  if (rowsSinceSpaceCheck >= 200) {
    rowsSinceSpaceCheck = 0;
    updateSdSpaceInfo();
    if (sdCritical) {
      Serial.println("[SD] CRITICAL space reached mid-log — flushing & closing");
      logFile.flush();
      logFile.close();
      // Leave sdOk=true so the WebUI/upload still works; just stop appending.
    }
  }
}
