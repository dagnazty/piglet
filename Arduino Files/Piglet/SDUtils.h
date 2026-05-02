#pragma once
#include <Arduino.h>

bool   openLogFile();
// closeLogFile returns true if the file was flushed AND a post-close stat
// confirms the file exists with non-zero size. False means the last write
// may not have made it to the card.
bool   closeLogFile();
void   appendWigleRow(const String& mac, const String& ssid, const String& auth,
                      const String& firstSeen, int channel, int rssi,
                      double lat, double lon, double altM, double accM);

// Refresh sdFreeBytes / sdTotalBytes / sdLowSpace / sdCritical from the card.
// Cheap to call (a couple of stat ops) but not free — call periodically, not per row.
void   updateSdSpaceInfo();

String normalizeSdPath(const char* dir, const char* nameIn);
String pathBasename(const String& p);
bool   isAllowedDataPath(const String& p);
bool   moveToUploaded(const String& srcPath);
