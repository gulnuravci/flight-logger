#pragma once

// Fetches the current altimeter setting (QNH) for a given ICAO airport ID
// from the NOAA METAR feed over HTTP.
//
// On success, writes the sea level pressure in hPa to *hpa and returns 0.
// On failure returns nonzero and leaves *hpa unchanged.
//
// Example: wx_fetch_altimeter("KABE", &hpa)
//   Fetches: http://tgftp.nws.noaa.gov/data/observations/metar/stations/KABE.TXT
//   METAR:   KABE 261853Z 24008KT 10SM SCT250 22/09 A3012 ...
//   Result:  1019.7 hPa  (30.12 inHg * 33.8639)

int wx_fetch_altimeter(const char *icao_id, float *hpa);
