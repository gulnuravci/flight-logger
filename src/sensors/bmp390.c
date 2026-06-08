#include "bmp390.h"
#include <math.h>
#include <string.h>

#define CHIP_ID_VAL  0x60

// PWR_CTRL: temp_en | press_en | normal mode
#define PWR_CTRL_VAL 0x33
// OSR: pressure 4x oversampling, temperature 2x
#define OSR_VAL      0x09
// ODR: 50 Hz
#define ODR_VAL      0x03
// IIR filter coefficient = 3
#define IIR_VAL      0x06

static void reg_write(i2c_inst_t *i2c, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_write_blocking(i2c, BMP390_ADDR, buf, 2, false);
}

static void reg_read(i2c_inst_t *i2c, uint8_t reg, uint8_t *dst, size_t len) {
    i2c_write_blocking(i2c, BMP390_ADDR, &reg, 1, true);
    i2c_read_blocking(i2c, BMP390_ADDR, dst, len, false);
}

// Parse raw NVM bytes into double-precision compensation coefficients.
// Scaling factors are defined in the BMP390 datasheet Section 9.
static void parse_calib(const uint8_t *raw, bmp390_calib_t *c) {
    uint16_t T1_raw = (uint16_t)(raw[1] << 8 | raw[0]);
    uint16_t T2_raw = (uint16_t)(raw[3] << 8 | raw[2]);
    int8_t   T3_raw = (int8_t)raw[4];

    int16_t  P1_raw  = (int16_t)(raw[6]  << 8 | raw[5]);
    int16_t  P2_raw  = (int16_t)(raw[8]  << 8 | raw[7]);
    int8_t   P3_raw  = (int8_t)raw[9];
    int8_t   P4_raw  = (int8_t)raw[10];
    uint16_t P5_raw  = (uint16_t)(raw[12] << 8 | raw[11]);
    uint16_t P6_raw  = (uint16_t)(raw[14] << 8 | raw[13]);
    int8_t   P7_raw  = (int8_t)raw[15];
    int8_t   P8_raw  = (int8_t)raw[16];
    int16_t  P9_raw  = (int16_t)(raw[18] << 8 | raw[17]);
    int8_t   P10_raw = (int8_t)raw[19];
    int8_t   P11_raw = (int8_t)raw[20];

    c->T1  = (double)T1_raw / pow(2.0, -8.0);
    c->T2  = (double)T2_raw / pow(2.0,  30.0);
    c->T3  = (double)T3_raw / pow(2.0,  48.0);

    c->P1  = ((double)P1_raw  - pow(2.0, 14.0)) / pow(2.0, 20.0);
    c->P2  = ((double)P2_raw  - pow(2.0, 14.0)) / pow(2.0, 29.0);
    c->P3  = (double)P3_raw  / pow(2.0, 32.0);
    c->P4  = (double)P4_raw  / pow(2.0, 37.0);
    c->P5  = (double)P5_raw  / pow(2.0, -3.0);
    c->P6  = (double)P6_raw  / pow(2.0,  6.0);
    c->P7  = (double)P7_raw  / pow(2.0,  8.0);
    c->P8  = (double)P8_raw  / pow(2.0, 15.0);
    c->P9  = (double)P9_raw  / pow(2.0, 48.0);
    c->P10 = (double)P10_raw / pow(2.0, 48.0);
    c->P11 = (double)P11_raw / pow(2.0, 65.0);
}

// Bosch compensation formula from BMP390 datasheet Section 9.3.
static double compensate_temperature(const bmp390_calib_t *c, uint32_t adc_T) {
    double pd1 = (double)adc_T - c->T1;
    double pd2 = c->T2 * pd1;
    double pd3 = pd1 * pd1 * c->T3;
    return pd2 + pd3;
}

// Section 9.4 — must call compensate_temperature first to get t_lin.
static double compensate_pressure(const bmp390_calib_t *c, uint32_t adc_P, double t_lin) {
    double pd1, pd2, pd3, pd4;

    pd1 = c->P6 * t_lin;
    pd2 = c->P7 * t_lin * t_lin;
    pd3 = c->P8 * t_lin * t_lin * t_lin;
    double out1 = c->P5 + pd1 + pd2 + pd3;

    pd1 = c->P2 * t_lin;
    pd2 = c->P3 * t_lin * t_lin;
    pd3 = c->P4 * t_lin * t_lin * t_lin;
    double out2 = (double)adc_P * (c->P1 + pd1 + pd2 + pd3);

    pd1 = (double)adc_P * (double)adc_P;
    pd2 = c->P9 + c->P10 * t_lin;
    pd3 = pd1 * pd2;
    pd4 = pd3 + c->P11 * (double)adc_P * (double)adc_P * (double)adc_P;

    return out1 + out2 + pd4;  // Pa
}

int bmp390_init(i2c_inst_t *i2c, bmp390_t *dev) {
    uint8_t id;
    reg_read(i2c, BMP390_CHIP_ID, &id, 1);
    if (id != CHIP_ID_VAL)
        return -1;

    uint8_t calib_raw[21];
    reg_read(i2c, BMP390_CALIB_DATA, calib_raw, 21);
    parse_calib(calib_raw, &dev->calib);

    dev->sea_level_hpa = BMP390_SEA_LEVEL_HPA;

    reg_write(i2c, BMP390_IIR,      IIR_VAL);
    reg_write(i2c, BMP390_OSR,      OSR_VAL);
    reg_write(i2c, BMP390_ODR,      ODR_VAL);
    reg_write(i2c, BMP390_PWR_CTRL, PWR_CTRL_VAL);
    return 0;
}

void bmp390_read(i2c_inst_t *i2c, bmp390_t *dev, bmp390_data_t *out) {
    uint8_t raw[6];
    reg_read(i2c, BMP390_DATA_0, raw, 6);

    // 24-bit little-endian pressure and temperature
    uint32_t adc_P = (uint32_t)raw[2] << 16 | (uint32_t)raw[1] << 8 | raw[0];
    uint32_t adc_T = (uint32_t)raw[5] << 16 | (uint32_t)raw[4] << 8 | raw[3];

    double t_lin = compensate_temperature(&dev->calib, adc_T);
    double press_pa = compensate_pressure(&dev->calib, adc_P, t_lin);

    out->temperature = (float)t_lin;
    out->pressure    = (float)(press_pa / 100.0);  // Pa → hPa

    // International barometric formula
    out->altitude = 44330.0f * (1.0f - powf(out->pressure / dev->sea_level_hpa, 0.1903f));
}
