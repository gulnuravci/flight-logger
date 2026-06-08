#pragma once

#include "credentials.h"

#define WIFI_CONNECT_TIMEOUT_MS 20000

// Attempt to connect using stored credentials.
// Returns 0 on success, nonzero on failure.
int wifi_connect(const wifi_credentials_t *creds);
