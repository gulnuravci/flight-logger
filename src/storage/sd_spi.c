#include "sd_spi.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <string.h>

// --- SD SPI command set ---
#define CMD0   0    // GO_IDLE_STATE
#define CMD8   8    // SEND_IF_COND
#define CMD9   9    // SEND_CSD (reads 16-byte CSD register)
#define CMD16  16   // SET_BLOCKLEN
#define CMD17  17   // READ_SINGLE_BLOCK
#define CMD24  24   // WRITE_BLOCK
#define CMD55  55   // APP_CMD (prefix for ACMD)
#define CMD58  58   // READ_OCR
#define ACMD41 41   // SD_SEND_OP_COND

// R1 response flags
#define R1_IDLE      0x01
#define R1_ERASE_RST 0x02
#define R1_ILL_CMD   0x04
#define R1_CRC_ERR   0x08
#define R1_ERASE_ERR 0x10
#define R1_ADDR_ERR  0x20
#define R1_PARAM_ERR 0x40

// Data tokens
#define TOKEN_START_BLOCK  0xFE
#define TOKEN_DATA_ACCEPT  0x05  // lower nibble of data response

static bool card_is_sdhc = false;

// --- Low-level SPI helpers ---

static inline void cs_low(void)  { gpio_put(SD_CS_PIN, 0); }
static inline void cs_high(void) { gpio_put(SD_CS_PIN, 1); }

static uint8_t spi_byte(uint8_t out) {
    uint8_t in;
    spi_write_read_blocking(SD_SPI_PORT, &out, &in, 1);
    return in;
}

// Send 0xFF while waiting for a non-0xFF response (up to n bytes).
static uint8_t wait_ready(int n) {
    uint8_t r;
    do { r = spi_byte(0xFF); } while (r == 0xFF && --n);
    return r;
}

// --- Command / response ---

// Send a 6-byte SD command and return the R1 response byte.
static uint8_t send_cmd(uint8_t cmd, uint32_t arg) {
    // Each command is preceded by at least one 0xFF clock byte.
    spi_byte(0xFF);

    spi_byte(0x40 | cmd);
    spi_byte((arg >> 24) & 0xFF);
    spi_byte((arg >> 16) & 0xFF);
    spi_byte((arg >>  8) & 0xFF);
    spi_byte((arg >>  0) & 0xFF);

    // CRC is only checked for CMD0 and CMD8 in SPI mode;
    // send correct CRCs for those, 0x01 (stop bit) for everything else.
    uint8_t crc = 0x01;
    if (cmd == CMD0) crc = 0x95;
    if (cmd == CMD8) crc = 0x87;
    spi_byte(crc);

    // The R1 response can take up to 8 bytes to arrive.
    uint8_t r1;
    for (int i = 0; i < 8; i++) {
        r1 = spi_byte(0xFF);
        if (!(r1 & 0x80)) break;
    }
    return r1;
}

// --- Initialisation ---

int sd_init(void) {
    // 1. Configure SPI hardware at 400 kHz (SD init requirement)
    spi_init(SD_SPI_PORT, 400 * 1000);
    gpio_set_function(SD_SCK_PIN,  GPIO_FUNC_SPI);
    gpio_set_function(SD_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SD_MISO_PIN, GPIO_FUNC_SPI);

    gpio_init(SD_CS_PIN);
    gpio_set_dir(SD_CS_PIN, GPIO_OUT);
    cs_high();

    // 2. Send >=74 clock cycles with CS high to wake the card.
    for (int i = 0; i < 10; i++) spi_byte(0xFF);

    // 3. CMD0: software reset → card enters SPI idle mode (R1 = 0x01)
    cs_low();
    uint8_t r1 = send_cmd(CMD0, 0);
    cs_high();
    spi_byte(0xFF);
    if (r1 != R1_IDLE) return -1;

    // 4. CMD8: check for SDv2 (voltage range 2.7-3.6V, check pattern 0xAA)
    cs_low();
    r1 = send_cmd(CMD8, 0x1AA);
    bool is_v2 = (r1 == R1_IDLE);
    if (is_v2) {
        // Read the 4-byte R7 trailer; check pattern must echo back
        uint8_t r7[4];
        for (int i = 0; i < 4; i++) r7[i] = spi_byte(0xFF);
        if ((r7[3] & 0xFF) != 0xAA) { cs_high(); return -1; }
    }
    cs_high();
    spi_byte(0xFF);

    // 5. ACMD41: send host capacity support (HCS=1 for SDHC/SDXC)
    uint32_t acmd41_arg = is_v2 ? 0x40000000 : 0;
    uint32_t timeout = 2000;
    do {
        cs_low();
        send_cmd(CMD55, 0);
        cs_high();
        spi_byte(0xFF);

        cs_low();
        r1 = send_cmd(ACMD41, acmd41_arg);
        cs_high();
        spi_byte(0xFF);
        sleep_ms(1);
    } while (r1 == R1_IDLE && --timeout);

    if (r1 != 0x00) return -1;

    // 6. CMD58: read OCR to check CCS bit (determines SDHC vs SDSC)
    if (is_v2) {
        cs_low();
        r1 = send_cmd(CMD58, 0);
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = spi_byte(0xFF);
        cs_high();
        spi_byte(0xFF);
        card_is_sdhc = (ocr[0] & 0x40) != 0;
    }

    // 7. CMD16: set block length to 512 (only needed for SDSC)
    if (!card_is_sdhc) {
        cs_low();
        r1 = send_cmd(CMD16, SD_BLOCK_SIZE);
        cs_high();
        spi_byte(0xFF);
        if (r1 != 0x00) return -1;
    }

    // 8. Raise SPI clock to 12.5 MHz for normal operation
    spi_set_baudrate(SD_SPI_PORT, 12500 * 1000);
    return 0;
}

// --- Block read / write ---

int sd_read_block(uint32_t block, uint8_t *buf) {
    // SDHC uses block address; SDSC uses byte address.
    uint32_t addr = card_is_sdhc ? block : block * SD_BLOCK_SIZE;

    cs_low();
    uint8_t r1 = send_cmd(CMD17, addr);
    if (r1 != 0x00) { cs_high(); return -1; }

    // Wait for data start token 0xFE
    uint8_t token = 0xFF;
    for (int i = 0; i < 20000; i++) {
        token = spi_byte(0xFF);
        if (token == TOKEN_START_BLOCK) break;
    }
    if (token != TOKEN_START_BLOCK) { cs_high(); return -1; }

    // Read 512 data bytes
    for (int i = 0; i < SD_BLOCK_SIZE; i++)
        buf[i] = spi_byte(0xFF);

    // Discard 2 CRC bytes
    spi_byte(0xFF);
    spi_byte(0xFF);

    cs_high();
    spi_byte(0xFF);
    return 0;
}

uint32_t sd_get_sector_count(void) {
    cs_low();
    if (send_cmd(CMD9, 0) != 0x00) { cs_high(); return 0; }

    // CMD9 returns a 16-byte data block preceded by the 0xFE start token
    uint8_t token = 0xFF;
    for (int i = 0; i < 20000; i++) {
        token = spi_byte(0xFF);
        if (token == TOKEN_START_BLOCK) break;
    }
    if (token != TOKEN_START_BLOCK) { cs_high(); return 0; }

    uint8_t csd[16];
    for (int i = 0; i < 16; i++) csd[i] = spi_byte(0xFF);
    spi_byte(0xFF); spi_byte(0xFF);  // discard CRC

    cs_high();
    spi_byte(0xFF);

    if ((csd[0] >> 6) == 1) {
        // CSD v2 (SDHC/SDXC): C_SIZE[21:0] in csd[7..9]
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) |
                          ((uint32_t)csd[8]           <<  8) |
                           (uint32_t)csd[9];
        return (c_size + 1) * 1024;
    }
    // CSD v1 (SDSC): classic formula
    uint32_t read_bl_len  = csd[5] & 0x0F;
    uint32_t c_size       = (uint32_t)(csd[6] & 0x03) << 10 |
                            (uint32_t)csd[7]           <<  2 |
                            (uint32_t)(csd[8] >> 6);
    uint32_t c_size_mult  = (uint32_t)(csd[9]  & 0x03) << 1 |
                            (uint32_t)(csd[10] >> 7);
    uint32_t block_nr     = (c_size + 1) << (c_size_mult + 2);
    uint32_t block_len    = 1U << read_bl_len;
    return block_nr * (block_len / 512);
}

int sd_write_block(uint32_t block, const uint8_t *buf) {
    uint32_t addr = card_is_sdhc ? block : block * SD_BLOCK_SIZE;

    cs_low();
    uint8_t r1 = send_cmd(CMD24, addr);
    if (r1 != 0x00) { cs_high(); return -1; }

    // Send data start token then 512 bytes then dummy CRC
    spi_byte(0xFF);
    spi_byte(TOKEN_START_BLOCK);
    for (int i = 0; i < SD_BLOCK_SIZE; i++) spi_byte(buf[i]);
    spi_byte(0xFF);  // CRC high
    spi_byte(0xFF);  // CRC low

    // Data response token: lower nibble 0x5 = accepted
    uint8_t resp = spi_byte(0xFF) & 0x1F;
    if (resp != 0x05) { cs_high(); return -1; }

    // Wait while card is busy (MISO held low during internal write)
    for (int i = 0; i < 500000; i++) {
        if (spi_byte(0xFF) != 0x00) break;
    }

    cs_high();
    spi_byte(0xFF);
    return 0;
}
