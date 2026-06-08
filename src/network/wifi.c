#include "wifi.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>

#define CONNECT_TIMEOUT_MS 8000

// Try each saved network in order, return 0 on the first successful connection.
int wifi_connect(const wifi_credentials_t *creds) {
    for (int i = 0; i < creds->count; i++) {
        const wifi_network_t *net = &creds->networks[i];
        printf("Trying WiFi: %s ...\n", net->ssid);

        int rc = cyw43_arch_wifi_connect_timeout_ms(
            net->ssid,
            net->password,
            CYW43_AUTH_WPA2_AES_PSK,
            CONNECT_TIMEOUT_MS
        );

        if (rc == 0) {
            printf("Connected to: %s\n", net->ssid);
            return 0;
        }
        printf("Failed (error %d), trying next...\n", rc);
    }

    printf("No saved networks reachable.\n");
    return -1;
}
