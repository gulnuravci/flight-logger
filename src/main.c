#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/i2c.h"
#include "lwip/netif.h"
#include "lsm6dsox.h"
#include "bmp390.h"
#include "flash_fs.h"
#include "credentials.h"
#include "wifi.h"
#include "wx_fetch.h"
#include "logger.h"
#include "dhcpserver.h"
#include "httpserver.h"

// ---------------------------------------------------------------------------
// Flight Logger — unified mode
//
// Boot sequence:
//   1. Connect to home WiFi (STA) → fetch QNH altimeter setting → disconnect
//   2. Switch to AP mode (FlightLogger hotspot) — stays up forever
//   3. Initialize sensors + SD card
//   4. Wait for button press to start / stop recording
//
// LED:   slow blink (500 ms) = idle / ready, AP is up
//        solid on             = recording in progress
//
// Button (GP15 → GND):
//   press while idle      → start recording
//   press while recording → stop, save file, back to idle
//
// PWA is always accessible at http://192.168.4.1 — no mode switching needed.
// ---------------------------------------------------------------------------

#define I2C_PORT  i2c0
#define I2C_SDA   4
#define I2C_SCL   5

#define BTN_PIN          15    // momentary pushbutton, other side to GND
#define BTN_DEBOUNCE_MS  50

#define AIRPORT_ICAO  "KABE"   // for QNH fetch at boot
#define AP_SSID       "FlightLogger"
#define AP_PASS       "flightlog"   // min 8 chars for WPA2

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void blink_fault(void) {
    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); sleep_ms(100);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); sleep_ms(100);
    }
}

static void led(bool on) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
}

// Returns true exactly once per debounced button press (active-LOW).
static bool btn_poll(void) {
    static bool     held       = false;
    static bool     reported   = false;
    static uint32_t press_time = 0;

    bool low = (gpio_get(BTN_PIN) == 0);

    if (low && !held) {
        held       = true;
        reported   = false;
        press_time = (uint32_t)(time_us_64() / 1000);
    }
    if (!low) {
        held     = false;
        reported = false;
    }
    if (held && !reported &&
        (uint32_t)(time_us_64() / 1000) - press_time >= BTN_DEBOUNCE_MS) {
        reported = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(void) {
    stdio_init_all();
    sleep_ms(2000);   // wait for USB serial

    if (cyw43_arch_init()) return 1;

    // ---- Step 1: fetch QNH from home WiFi (STA mode) -----------------------

    float sea_level_hpa = BMP390_SEA_LEVEL_HPA;   // ISA fallback (1013.25 hPa)

    lfs_t lfs;
    struct lfs_config lfs_cfg;
    if (flash_fs_mount(&lfs, &lfs_cfg) == LFS_ERR_OK) {
        wifi_credentials_t creds;
        credentials_load(&lfs, &creds);

        if (wifi_connect(&creds) == 0) {
            float fetched_hpa;
            if (wx_fetch_altimeter(AIRPORT_ICAO, &fetched_hpa) == 0) {
                sea_level_hpa = fetched_hpa;
                printf("QNH (%s): %.1f hPa\n", AIRPORT_ICAO, sea_level_hpa);
            } else {
                printf("QNH fetch failed — using standard atmosphere\n");
            }
        } else {
            printf("Home WiFi unavailable — using standard atmosphere\n");
        }
    } else {
        printf("Flash FS error — using standard atmosphere\n");
    }

    // Disconnect from STA before starting AP
    cyw43_arch_deinit();
    if (cyw43_arch_init()) return 1;

    // ---- Step 2: start AP + HTTP server (stays up forever) -----------------

    cyw43_arch_enable_ap_mode(AP_SSID, AP_PASS, CYW43_AUTH_WPA2_AES_PSK);

    ip4_addr_t gw, nm;
    IP4_ADDR(&gw, 192, 168, 4, 1);
    IP4_ADDR(&nm, 255, 255, 255, 0);

    static dhcp_server_t dhcp;
    cyw43_arch_lwip_begin();
    dhcp_server_init(&dhcp, &gw, &nm);
    httpserver_init();
    cyw43_arch_lwip_end();

    printf("AP \"%s\" up — connect and open http://192.168.4.1\n", AP_SSID);

    // ---- Step 3: initialize sensors ----------------------------------------

    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    sleep_ms(100);

    if (lsm6dsox_init(I2C_PORT) != 0) blink_fault();

    bmp390_t baro;
    if (bmp390_init(I2C_PORT, &baro) != 0) blink_fault();
    baro.sea_level_hpa = sea_level_hpa;

    // Discard first barometer reading (Bosch recommendation)
    bmp390_data_t tmp;
    bmp390_read(I2C_PORT, &baro, &tmp);
    sleep_ms(100);

    // ---- Step 4: mount SD card ---------------------------------------------

    // Mount SD for HTTP server access (existing flights).
    // Don't gate recording on this — logger_init() re-mounts on button press.
    if (logger_mount() != 0)
        printf("SD card not found at boot — will retry on button press\n");

    // ---- Step 5: button + recording loop -----------------------------------

    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);
    gpio_pull_up(BTN_PIN);

    typedef enum { IDLE, RECORDING } state_t;
    state_t state = IDLE;

    bool led_on = false;
    absolute_time_t next_blink = make_timeout_time_ms(500);

    printf("Ready — press button (GP%d) to start recording\n", BTN_PIN);

    lsm6dsox_data_t imu;
    bmp390_data_t   bmp;

    while (true) {
        // Service WiFi / HTTP
        cyw43_arch_poll();
        httpserver_poll();

        // Read sensors
        lsm6dsox_read(I2C_PORT, &imu);
        bmp390_read(I2C_PORT, &baro, &bmp);

        printf("accel: %+.3f %+.3f %+.3f g   gyro: %+.1f %+.1f %+.1f dps   "
               "alt: %.1f m   temp: %.1f C\n",
               imu.ax, imu.ay, imu.az,
               imu.gx, imu.gy, imu.gz,
               bmp.altitude, bmp.temperature);

        // Button: toggle recording
        if (btn_poll()) {
            if (state == IDLE) {
                if (logger_init() == 0) {
                    state = RECORDING;
                    led(true);
                    printf("*** Recording started ***\n");
                } else {
                    printf("Cannot start — SD card not ready\n");
                    // 3 quick flashes = SD error
                    for (int i = 0; i < 3; i++) {
                        led(true);  sleep_ms(80);
                        led(false); sleep_ms(80);
                    }
                }
            } else {
                logger_close();
                state  = IDLE;
                led_on = false;
                led(false);
                next_blink = make_timeout_time_ms(500);
                printf("*** Recording stopped — file saved ***\n");
            }
        }

        // Write row if recording
        if (state == RECORDING)
            logger_write((uint32_t)(time_us_64() / 1000), &imu, &bmp);

        // Slow blink when idle
        if (state == IDLE &&
            absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
            led_on = !led_on;
            led(led_on);
            next_blink = make_timeout_time_ms(500);
        }

        sleep_ms(100);
    }
}
