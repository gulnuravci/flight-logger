#include "flash_fs.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include <string.h>

// Offset from the start of flash where the LittleFS partition begins.
#define LFS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - LFS_FLASH_SIZE)

// --- LittleFS block device callbacks ---

static int lfs_read_cb(const struct lfs_config *c, lfs_block_t block,
                       lfs_off_t off, void *buffer, lfs_size_t size) {
    uint32_t addr = LFS_FLASH_OFFSET + block * LFS_BLOCK_SIZE + off;
    memcpy(buffer, (const void *)(XIP_BASE + addr), size);
    return LFS_ERR_OK;
}

typedef struct { uint32_t addr; const uint8_t *data; size_t len; } prog_args_t;
typedef struct { uint32_t addr; size_t len; } erase_args_t;

static void do_flash_program(void *arg) {
    prog_args_t *a = (prog_args_t *)arg;
    flash_range_program(a->addr, a->data, a->len);
}

static void do_flash_erase(void *arg) {
    erase_args_t *a = (erase_args_t *)arg;
    flash_range_erase(a->addr, a->len);
}

static int lfs_prog_cb(const struct lfs_config *c, lfs_block_t block,
                       lfs_off_t off, const void *buffer, lfs_size_t size) {
    prog_args_t args = {
        .addr = LFS_FLASH_OFFSET + block * LFS_BLOCK_SIZE + off,
        .data = (const uint8_t *)buffer,
        .len  = size
    };
    // flash_safe_execute disables XIP cache and runs the operation from RAM,
    // preventing a hard fault when code-in-flash is erased/written beneath us.
    int rc = flash_safe_execute(do_flash_program, &args, UINT32_MAX);
    return (rc == PICO_OK) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int lfs_erase_cb(const struct lfs_config *c, lfs_block_t block) {
    erase_args_t args = {
        .addr = LFS_FLASH_OFFSET + block * LFS_BLOCK_SIZE,
        .len  = LFS_BLOCK_SIZE
    };
    int rc = flash_safe_execute(do_flash_erase, &args, UINT32_MAX);
    return (rc == PICO_OK) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int lfs_sync_cb(const struct lfs_config *c) {
    return LFS_ERR_OK;
}

// --- Public API ---

int flash_fs_mount(lfs_t *lfs, struct lfs_config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->read        = lfs_read_cb;
    cfg->prog        = lfs_prog_cb;
    cfg->erase       = lfs_erase_cb;
    cfg->sync        = lfs_sync_cb;
    cfg->read_size   = 1;
    cfg->prog_size   = FLASH_PAGE_SIZE;   // 256 bytes
    cfg->block_size  = LFS_BLOCK_SIZE;    // 4096 bytes
    cfg->block_count = LFS_BLOCK_COUNT;   // 64 blocks
    cfg->cache_size  = FLASH_PAGE_SIZE;
    cfg->lookahead_size = 16;
    cfg->block_cycles = 500;

    int err = lfs_mount(lfs, cfg);
    if (err != LFS_ERR_OK) {
        // First boot or filesystem corruption — format and remount.
        lfs_format(lfs, cfg);
        err = lfs_mount(lfs, cfg);
    }
    return err;
}
