#pragma once

#include "lfs.h"

#define CRED_SSID_MAX      64
#define CRED_PASSWORD_MAX  64
#define CRED_MAX_NETWORKS  4

typedef struct {
    char ssid[CRED_SSID_MAX];
    char password[CRED_PASSWORD_MAX];
} wifi_network_t;

typedef struct {
    wifi_network_t networks[CRED_MAX_NETWORKS];
    int            count;
} wifi_credentials_t;

// Load saved networks from LittleFS. If none exist, prompts over USB serial
// to add the first one. Returns with at least one network populated.
void credentials_load(lfs_t *lfs, wifi_credentials_t *creds);

// Persist the current credentials list to flash.
void credentials_save(lfs_t *lfs, const wifi_credentials_t *creds);

// Interactive: prompt to add another network (up to CRED_MAX_NETWORKS).
// Called from the serial console when the user wants to add their hotspot.
void credentials_add_network(lfs_t *lfs, wifi_credentials_t *creds);
