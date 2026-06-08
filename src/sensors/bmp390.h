#pragma once

#include "hardware/i2c.h"

#define BMP390_ADDR         0x77

#define BMP390_CHIP_ID      0x00  // should read 0x60
#define BMP390_STATUS       0x03
#define BMP390_DATA_0       0x04  // pressure XLSB (first of 6 data bytes)
#define BMP390_PWR_CTRL     0x1B
#define BMP390_OSR          0x1C
#define BMP390_ODR          0x1D
#define BMP390_IIR          0x1F
#define BMP390_CALIB_DATA   0x31  // first of 21 calibration bytes

// Standard atmosphere sea level pressure in hPa.
// Override this with current METAR pressure for accurate absolute altitude.
#define BMP390_SEA_LEVEL_HPA 1013.25f

typedef struct {
    float temperature;  // degrees C
    float pressure;     // hPa
    float altitude;     // meters above sea level (relative to sea_level_hpa)
} bmp390_data_t;

// calib holds the processed double-precision compensation coefficients
typedef struct {
    double T1, T2, T3;
    double P1, P2, P3, P4, P5, P6, P7, P8, P9, P10, P11;
} bmp390_calib_t;

typedef struct {
    bmp390_calib_t calib;
    float sea_level_hpa;
} bmp390_t;

int  bmp390_init(i2c_inst_t *i2c, bmp390_t *dev);
void bmp390_read(i2c_inst_t *i2c, bmp390_t *dev, bmp390_data_t *out);
