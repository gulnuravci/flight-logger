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
// Pin / peripheral assignments
// ---------------------------------------------------------------------------

#define I2C_PORT  i2c0
#define I2C_SDA   4
#define I2C_SCL   5

// Hold GP22 LOW at power-on (button to GND) to enter server / review mode.
// Release (or leave unconnected) for the default flight-logging mode.
#define MODE_PIN  22

#define AIRPORT_ICAO "KABE"   // used for METAR altimeter fetch in logger mode

// AP credentials (server mode)
#define AP_SSID "FlightLogger"
#define AP_PASS "flightlog"   // min 8 chars for WPA2

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static void blink_fault(void) {
    // Rapid blink = something went badly wrong at startup.
    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); sleep_ms(100);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); sleep_ms(100);
    }
}

static void led(bool on) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
}

static void init_i2c_sensors(bmp390_t *baro, float sea_level_hpa) {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    sleep_ms(100);

    if (lsm6dsox_init(I2C_PORT) != 0) blink_fault();
    if (bmp390_init(I2C_PORT, baro) != 0) blink_fault();

    baro->sea_level_hpa = sea_level_hpa;

    // Discard first barometer reading (Bosch recommendation)
    bmp390_data_t tmp;
    bmp390_read(I2C_PORT, baro, &tmp);
    sleep_ms(100);
}

// ---------------------------------------------------------------------------
// Server mode  (AP hotspot + HTTP flight-data API)
// ---------------------------------------------------------------------------

static void run_server_mode(void) {
    printf("Entering SERVER mode — FlightLogger hotspot\n");

    // Mount SD card so the HTTP server can read existing flight files.
    if (logger_mount() != 0)
        printf("Warning: SD card not found — no flights will be served\n");

    // Start WiFi AP
    cyw43_arch_enable_ap_mode(AP_SSID, AP_PASS, CYW43_AUTH_WPA2_AES_PSK);

    // CYW43 AP default gateway is 192.168.4.1 — configure DHCP server with that.
    ip4_addr_t gw, nm;
    IP4_ADDR(&gw, 192, 168, 4, 1);
    IP4_ADDR(&nm, 255, 255, 255, 0);

    static dhcp_server_t dhcp;
    cyw43_arch_lwip_begin();
    dhcp_server_init(&dhcp, &gw, &nm);
    httpserver_init();
    cyw43_arch_lwip_end();

    printf("Hotspot \"%s\" up — connect and open http://192.168.4.1\n", AP_SSID);

    // Slow blink to indicate server mode (1 s on / 1 s off)
    bool led_state = false;
    absolute_time_t next_blink = make_timeout_time_ms(1000);

    while (true) {
        cyw43_arch_poll();
        httpserver_poll();

        if (absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
            led_state = !led_state;
            led(led_state);
            next_blink = make_timeout_time_ms(1000);
        }

        cyw43_arch_wait_for_work_until(
            absolute_time_min(next_blink, make_timeout_time_ms(5)));
    }
}

// ---------------------------------------------------------------------------
// Logger mode  (sensor read + CSV write every 100 ms)
// ---------------------------------------------------------------------------

static void run_logger_mode(void) {
    printf("Entering LOGGER mode\n");

    // Flash filesystem + WiFi credentials
    lfs_t lfs;
    struct lfs_config lfs_cfg;
    if (flash_fs_mount(&lfs, &lfs_cfg) != LFS_ERR_OK)
        blink_fault();

    wifi_credentials_t creds;
    credentials_load(&lfs, &creds);

    // Connect to WiFi to fetch the current altimeter setting (QNH)
    float sea_level_hpa = BMP390_SEA_LEVEL_HPA;  // ISA fallback

    if (wifi_connect(&creds) == 0) {
        float fetched_hpa;
        if (wx_fetch_altimeter(AIRPORT_ICAO, &fetched_hpa) == 0) {
            sea_level_hpa = fetched_hpa;
            printf("Altimeter (%s): %.1f hPa\n", AIRPORT_ICAO, sea_level_hpa);
        } else {
            printf("wx_fetch failed — using standard atmosphere (%.2f hPa)\n",
                   sea_level_hpa);
        }
        cyw43_arch_deinit();
        cyw43_arch_init();   // re-init for LED-only use
    } else {
        printf("WiFi failed — using standard atmosphere\n");
        cyw43_arch_deinit();
        cyw43_arch_init();
    }

    // I2C sensors
    bmp390_t baro;
    init_i2c_sensors(&baro, sea_level_hpa);

    // SD card logger
    bool logging = (logger_init() == 0);
    if (!logging)
        printf("SD card not found — logging disabled\n");

    // Steady LED = logging active
    led(true);

    // Main 100 ms loop
    lsm6dsox_data_t imu;
    bmp390_data_t   bmp;

    while (true) {
        lsm6dsox_read(I2C_PORT, &imu);
        bmp390_read(I2C_PORT, &baro, &bmp);

        printf("accel: %+.3f %+.3f %+.3f g   gyro: %+.1f %+.1f %+.1f dps   "
               "alt: %.1f m   temp: %.1f C\n",
               imu.ax, imu.ay, imu.az,
               imu.gx, imu.gy, imu.gz,
               bmp.altitude, bmp.temperature);

        if (logging)
            logger_write((uint32_t)(time_us_64() / 1000), &imu, &bmp);

        sleep_ms(100);
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(void) {
    stdio_init_all();
    sleep_ms(2000);  // wait for USB serial to enumerate

    if (cyw43_arch_init()) return 1;

    // Check mode pin: LOW at boot → server mode, HIGH (default) → logger mode
    gpio_init(MODE_PIN);
    gpio_set_dir(MODE_PIN, GPIO_IN);
    gpio_pull_up(MODE_PIN);
    sleep_ms(10);  // settle
    bool server_mode = (gpio_get(MODE_PIN) == 0);

    if (server_mode)
        run_server_mode();   // never returns
    else
        run_logger_mode();   // never returns

    return 0;
}
