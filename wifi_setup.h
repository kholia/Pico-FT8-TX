///////////////////////////////////////////////////////////////////////////////
//
//  WiFi Setup Module - AP Mode Configuration
//
//  Provides AP mode setup for initial WiFi configuration
//
///////////////////////////////////////////////////////////////////////////////

#ifndef WIFI_SETUP_H_
#define WIFI_SETUP_H_

#include <stdbool.h>
#include <stdint.h>
#include "util/flashmem.h"

// Maximum number of networks to scan
#define WIFI_MAX_NETWORKS 20

// Network information structure
typedef struct {
    char ssid[32];
    int rssi;
    int auth_mode;
} wifi_network_t;

// WiFi setup context
typedef struct {
    FlashMemContext flash_ctx;
    wifi_network_t networks[WIFI_MAX_NETWORKS];
    int network_count;
    bool ap_mode_active;
} wifi_setup_context_t;

// Initialize WiFi setup system
bool wifi_setup_init(wifi_setup_context_t *ctx);

// Check if WiFi credentials are stored in flash
bool wifi_setup_has_credentials(wifi_setup_context_t *ctx);

// Get stored WiFi credentials
bool wifi_setup_get_credentials(wifi_setup_context_t *ctx, char *ssid, char *password,
                               size_t ssid_size, size_t password_size);

// Get stored station configuration
bool wifi_setup_get_station_config(wifi_setup_context_t *ctx, char *callsign, char *locator,
                                  size_t callsign_size, size_t locator_size);

// Get stored frequency configuration
bool wifi_setup_get_frequency_config(wifi_setup_context_t *ctx, uint32_t *frequency);

// Start AP mode for configuration
bool wifi_setup_start_ap(wifi_setup_context_t *ctx);

// Scan for available WiFi networks
bool wifi_setup_scan_networks(wifi_setup_context_t *ctx);

// Get scanned networks
const wifi_network_t* wifi_setup_get_networks(wifi_setup_context_t *ctx, int *count);

// Save WiFi and station configuration to flash
bool wifi_setup_save_config(wifi_setup_context_t *ctx, const char *ssid, const char *password, const char *callsign, const char *locator, uint32_t frequency);

// Stop AP mode and switch to STA mode
bool wifi_setup_stop_ap(wifi_setup_context_t *ctx);

// Serial command functions for setup
void wifi_setup_process_serial_command(const char *command);
bool wifi_setup_is_setup_complete(void);

#endif
