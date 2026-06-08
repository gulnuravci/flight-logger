#pragma once

// LittleFS block device backed by Pico 2W internal flash.
// Reserves the last LFS_SIZE bytes of flash for the filesystem.
// The firmware itself lives at the start of flash and is typically < 1MB,
// so a 256KB filesystem at the top of 4MB flash is safe.

#include "lfs.h"

#define LFS_FLASH_SIZE   (256 * 1024)   // 256KB filesystem partition
#define LFS_BLOCK_SIZE   4096           // RP2350 flash erase sector size
#define LFS_BLOCK_COUNT  (LFS_FLASH_SIZE / LFS_BLOCK_SIZE)

// Initialise the lfs_config struct and mount (or format+mount) the filesystem.
// Returns 0 on success.
int flash_fs_mount(lfs_t *lfs, struct lfs_config *cfg);
