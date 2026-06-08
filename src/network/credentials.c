#include "credentials.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

#define CRED_FILE "/credentials.bin"

static void read_line(char *buf, int maxlen) {
    int i = 0;
    while (i < maxlen - 1) {
        int c = getchar();
        if (c == '\r' || c == '\n') break;
        buf[i++] = (char)c;
    }
    buf[i] = '\0';
}

static void prompt_network(wifi_network_t *net) {
    printf("  SSID: ");
    read_line(net->ssid, CRED_SSID_MAX);
    printf("\n  Password: ");
    read_line(net->password, CRED_PASSWORD_MAX);
    printf("\n");
}

void credentials_add_network(lfs_t *lfs, wifi_credentials_t *creds) {
    if (creds->count >= CRED_MAX_NETWORKS) {
        printf("Network list full (%d/%d).\n", creds->count, CRED_MAX_NETWORKS);
        return;
    }
    printf("--- Add WiFi Network (%d/%d) ---\n", creds->count + 1, CRED_MAX_NETWORKS);
    prompt_network(&creds->networks[creds->count]);
    creds->count++;
    credentials_save(lfs, creds);
    printf("Saved.\n");
}

void credentials_load(lfs_t *lfs, wifi_credentials_t *creds) {
    memset(creds, 0, sizeof(*creds));

    lfs_file_t file;
    if (lfs_file_open(lfs, &file, CRED_FILE, LFS_O_RDONLY) == LFS_ERR_OK) {
        lfs_file_read(lfs, &file, creds, sizeof(*creds));
        lfs_file_close(lfs, &file);
    }

    if (creds->count > 0 && creds->networks[0].ssid[0] != '\0')
        return;

    // First boot — provision at least one network
    printf("\n--- Flight Logger: First-time WiFi Setup ---\n");
    printf("You can add up to %d networks (e.g. home WiFi + phone hotspot).\n\n",
           CRED_MAX_NETWORKS);
    credentials_add_network(lfs, creds);

    printf("Add another network? (y/n): ");
    int c = getchar();
    printf("\n");
    while (c == 'y' || c == 'Y') {
        credentials_add_network(lfs, creds);
        printf("Add another? (y/n): ");
        c = getchar();
        printf("\n");
    }
}

void credentials_save(lfs_t *lfs, const wifi_credentials_t *creds) {
    lfs_file_t file;
    lfs_file_open(lfs, &file, CRED_FILE, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    lfs_file_write(lfs, &file, creds, sizeof(*creds));
    lfs_file_close(lfs, &file);
}
