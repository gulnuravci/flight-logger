#include "logger.h"
#include "ff.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static FATFS  fs;
static FIL    log_file;
static bool   file_open  = false;
static uint32_t row_count = 0;

int logger_mount(void) {
    return (f_mount(&fs, "", 1) == FR_OK) ? 0 : -1;
}

int logger_init(void) {
    // Mount drive 0; immediate=1 forces a disk_initialize() right now.
    if (f_mount(&fs, "", 1) != FR_OK)
        return -1;

    // Find the next unused FLT_NNN.CSV slot (1–999).
    char fname[16];
    int  n;
    for (n = 1; n <= 999; n++) {
        snprintf(fname, sizeof(fname), "FLT_%03d.CSV", n);
        FILINFO fi;
        if (f_stat(fname, &fi) == FR_NO_FILE)
            break;
        if (n == 999)
            return -1;  // all slots taken
    }

    if (f_open(&log_file, fname, FA_WRITE | FA_CREATE_NEW) != FR_OK)
        return -1;

    file_open = true;
    row_count = 0;

    f_puts("timestamp_ms,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,"
           "altitude_m,pressure_hpa,temperature_c\n", &log_file);
    f_sync(&log_file);
    return 0;
}

void logger_write(uint32_t timestamp_ms,
                  const lsm6dsox_data_t *imu,
                  const bmp390_data_t   *bmp) {
    if (!file_open) return;

    char line[128];
    int len = snprintf(line, sizeof(line),
        "%lu,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.1f,%.2f,%.1f\n",
        (unsigned long)timestamp_ms,
        imu->ax, imu->ay, imu->az,
        imu->gx, imu->gy, imu->gz,
        bmp->altitude, bmp->pressure, bmp->temperature);

    UINT bw;
    f_write(&log_file, line, (UINT)len, &bw);

    if (++row_count % 100 == 0)
        f_sync(&log_file);
}

void logger_sync(void) {
    if (file_open) f_sync(&log_file);
}
