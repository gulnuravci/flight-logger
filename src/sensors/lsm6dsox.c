#include "lsm6dsox.h"
#include <string.h>

#define WHO_AM_I_VAL 0x6C

// CTRL1_XL: ODR = 104Hz, full scale = ±4g
#define CTRL1_XL_VAL 0x4A
// CTRL2_G:  ODR = 104Hz, full scale = ±500 dps
#define CTRL2_G_VAL  0x44

// Sensitivity constants from datasheet
#define ACCEL_SCALE  (0.122f / 1000.0f)  // mg/LSB → g/LSB  (±4g, 12-bit)
#define GYRO_SCALE   (17.50f / 1000.0f)  // mdps/LSB → dps/LSB (±500dps)

static void reg_write(i2c_inst_t *i2c, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_write_blocking(i2c, LSM6DSOX_ADDR, buf, 2, false);
}

static void reg_read(i2c_inst_t *i2c, uint8_t reg, uint8_t *dst, size_t len) {
    i2c_write_blocking(i2c, LSM6DSOX_ADDR, &reg, 1, true);  // true = keep bus
    i2c_read_blocking(i2c, LSM6DSOX_ADDR, dst, len, false);
}

int lsm6dsox_init(i2c_inst_t *i2c) {
    uint8_t id;
    reg_read(i2c, LSM6DSOX_WHO_AM_I, &id, 1);
    if (id != WHO_AM_I_VAL)
        return -1;

    reg_write(i2c, LSM6DSOX_CTRL1_XL, CTRL1_XL_VAL);
    reg_write(i2c, LSM6DSOX_CTRL2_G,  CTRL2_G_VAL);
    return 0;
}

void lsm6dsox_read(i2c_inst_t *i2c, lsm6dsox_data_t *out) {
    uint8_t raw[12];

    reg_read(i2c, LSM6DSOX_OUTX_L_G, raw, 6);
    int16_t gx = (int16_t)(raw[1] << 8 | raw[0]);
    int16_t gy = (int16_t)(raw[3] << 8 | raw[2]);
    int16_t gz = (int16_t)(raw[5] << 8 | raw[4]);

    reg_read(i2c, LSM6DSOX_OUTX_L_A, raw, 6);
    int16_t ax = (int16_t)(raw[1] << 8 | raw[0]);
    int16_t ay = (int16_t)(raw[3] << 8 | raw[2]);
    int16_t az = (int16_t)(raw[5] << 8 | raw[4]);

    out->gx = gx * GYRO_SCALE;
    out->gy = gy * GYRO_SCALE;
    out->gz = gz * GYRO_SCALE;
    out->ax = ax * ACCEL_SCALE;
    out->ay = ay * ACCEL_SCALE;
    out->az = az * ACCEL_SCALE;
}
