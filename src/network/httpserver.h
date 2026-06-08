#pragma once

// Minimal HTTP/1.0 server that exposes two API endpoints:
//
//   GET /api/flights        → JSON array of FlightMeta objects
//   GET /api/flight/<n>     → raw CSV text of FLT_0nn.CSV
//
// Only one connection is handled at a time.
// Call httpserver_poll() from the main loop to drive CSV streaming.

void httpserver_init(void);
void httpserver_poll(void);
