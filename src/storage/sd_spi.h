#pragma once

#include <stdint.h>

// SPI1 pin assignments for the SparkFun Level Shifting microSD breakout.
// These are free of the I2C pins (GPIO 4/5) used by the sensors.
#define SD_SPI_PORT  spi1
#define SD_SCK_PIN   10
#define SD_MOSI_PIN  11
#define SD_MISO_PIN  12
#define SD_CS_PIN    13

#define SD_BLOCK_SIZE 512

// Initialise the SD card over SPI. Returns 0 on success.
// Call once after power-up. Leaves SPI clock at 12.5 MHz.
int sd_init(void);

// Read one 512-byte block. block is a block number (not a byte address).
int sd_read_block(uint32_t block, uint8_t *buf);

// Write one 512-byte block.
int sd_write_block(uint32_t block, const uint8_t *buf);

// Return total sector count (needed by FatFs diskio GET_SECTOR_COUNT).
// Returns 0 on failure.
uint32_t sd_get_sector_count(void);
