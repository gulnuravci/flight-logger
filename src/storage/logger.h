#pragma once

#include <stdint.h>
#include "lsm6dsox.h"
#include "bmp390.h"

// Mount the SD card filesystem without opening a log file.
// Use in server mode to make existing flights readable without starting a new one.
// Returns 0 on success, -1 on failure.
int logger_mount(void);

// Mount + open the next available FLT_NNN.CSV for writing.
// Returns 0 on success, -1 on failure (no card or filesystem error).
int logger_init(void);

// Append one CSV row. timestamp_ms is milliseconds since boot.
// Flushes to card automatically every 100 rows.
void logger_write(uint32_t timestamp_ms,
                  const lsm6dsox_data_t *imu,
                  const bmp390_data_t   *bmp);

// Explicit flush — call before power-off or at the end of a flight.
void logger_sync(void);
