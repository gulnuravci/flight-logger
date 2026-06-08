#pragma once

#include "hardware/i2c.h"

#define LSM6DSOX_ADDR       0x6A

#define LSM6DSOX_WHO_AM_I   0x0F  // should read 0x6C
#define LSM6DSOX_CTRL1_XL   0x10  // accelerometer control
#define LSM6DSOX_CTRL2_G    0x11  // gyroscope control
#define LSM6DSOX_STATUS_REG 0x1E
#define LSM6DSOX_OUTX_L_G   0x22  // gyro X low byte (6 bytes: X,Y,Z)
#define LSM6DSOX_OUTX_L_A   0x28  // accel X low byte (6 bytes: X,Y,Z)

typedef struct {
    float ax, ay, az;  // acceleration in g
    float gx, gy, gz;  // angular rate in deg/s
} lsm6dsox_data_t;

int  lsm6dsox_init(i2c_inst_t *i2c);
void lsm6dsox_read(i2c_inst_t *i2c, lsm6dsox_data_t *out);
