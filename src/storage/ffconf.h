#pragma once

// FatFs configuration for flight-logger.
// This file is picked up by ff.c via #include "ffconf.h".

#define FFCONF_DEF 80286  // revision tag

#define FF_FS_READONLY   0   // read-write
#define FF_FS_MINIMIZE   0   // full API
#define FF_USE_STRFUNC   1   // f_puts, f_printf
#define FF_USE_FIND      0
#define FF_USE_MKFS      0   // no format support needed
#define FF_USE_FASTSEEK  0
#define FF_USE_EXPAND    0
#define FF_USE_CHMOD     0
#define FF_USE_LABEL     0
#define FF_USE_FORWARD   0
#define FF_USE_STRF_ENCODING 0

#define FF_CODE_PAGE     437  // US ASCII

#define FF_USE_LFN       0    // no long filename support — 8.3 names only
#define FF_MAX_LFN       255
#define FF_LFN_UNICODE   0
#define FF_LFN_BUF       255
#define FF_SFN_BUF       12
#define FF_STRF_ENCODE   3

#define FF_FS_RPATH      0

#define FF_VOLUMES       1
#define FF_STR_VOLUME_ID 0
#define FF_VOLUME_STRS   "SD"
#define FF_MULTI_PARTITION 0
#define FF_MIN_SS        512
#define FF_MAX_SS        512  // fixed 512-byte sectors
#define FF_LBA64         0
#define FF_MIN_GPT       0x10000000

#define FF_USE_TRIM      0
#define FF_FS_NOFSINFO   0

#define FF_FS_TINY       0
#define FF_FS_EXFAT      0
#define FF_FS_NORTC      1   // no RTC — use a fixed timestamp
#define FF_NORTC_MON     1
#define FF_NORTC_MDAY    1
#define FF_NORTC_YEAR    2026

#define FF_FS_LOCK       0
#define FF_FS_REENTRANT  0
