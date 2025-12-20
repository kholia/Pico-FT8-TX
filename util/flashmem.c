///////////////////////////////////////////////////////////////////////////////
//
//  WiFi Configuration Flash Storage Implementation
//
//  Stores WiFi credentials persistently in Pico flash memory
//
///////////////////////////////////////////////////////////////////////////////

#include "flashmem.h"
#include <string.h>
#include <stdio.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/flash.h"

// Flash configuration
// Use a safe offset within flash memory range
// Based on pico-examples flash programming patterns
#define FLASH_TARGET_OFFSET (512 * 1024)  // Use 512KB offset from start of flash
#define FLASH_CONFIG_MAGIC 0x57494649  // "WIFI" in ASCII

// WiFi and station configuration structure
typedef struct {
    uint32_t magic;           // Magic number to verify data validity
    char ssid[32];           // WiFi SSID (max 31 chars + null)
    char password[64];       // WiFi password (max 63 chars + null)
    char callsign[12];       // Station callsign (max 11 chars + null)
    char locator[7];         // Station locator (max 6 chars + null)
    uint32_t frequency;      // FT8 transmission frequency in Hz
    uint32_t checksum;       // Simple checksum for data integrity
} wifi_config_t;

// Calculate simple checksum
static uint32_t calculate_checksum(const wifi_config_t *config) {
    uint32_t sum = 0;
    const uint8_t *data = (const uint8_t *)config;
    for (size_t i = 0; i < sizeof(wifi_config_t) - sizeof(uint32_t); ++i) {
        sum += data[i];
    }
    return sum;
}

// Initialize flash memory context
void flashmem_init(FlashMemContext *ctx) {
    ctx->_pFlashTargetOffset = (void *)FLASH_TARGET_OFFSET;
    printf("[+] flashmem_init: Using flash offset 0x%08x\n", FLASH_TARGET_OFFSET);
    printf("[+] flashmem_init: XIP read address will be 0x%08x\n", XIP_BASE + FLASH_TARGET_OFFSET);
    printf("[+] flashmem_init: PICO_FLASH_SIZE_BYTES = %d, FLASH_SECTOR_SIZE = %d\n",
           PICO_FLASH_SIZE_BYTES, FLASH_SECTOR_SIZE);
}

// Read WiFi configuration from flash
bool flashmem_read_wifi_config(FlashMemContext *ctx, char *ssid, char *password, size_t ssid_size, size_t password_size) {
    // Validate parameters
    if (!ctx || !ssid || !password || ssid_size == 0 || password_size == 0) {
        printf("[!] flashmem_read_wifi_config: Invalid parameters\n");
        return false;
    }

    // Use XIP address for reading flash (following pico-examples pattern)
    uintptr_t xip_addr = XIP_BASE + (uintptr_t)ctx->_pFlashTargetOffset;
    printf("[+] flashmem_read_wifi_config: Reading from XIP address 0x%08x\n", (unsigned int)xip_addr);

    const wifi_config_t *config = (const wifi_config_t *)xip_addr;

    // Check if configuration exists and is valid
    uint32_t magic = config->magic;  // Read magic separately to avoid potential issues
    printf("[+] flashmem_read_wifi_config: Read magic 0x%08x\n", magic);

    if (magic != FLASH_CONFIG_MAGIC) {
        printf("[+] flashmem_read_wifi_config: No valid magic (found 0x%08x, expected 0x%08x)\n", magic, FLASH_CONFIG_MAGIC);
        return false;  // No valid configuration stored
    }

    // Verify checksum
    uint32_t stored_checksum = config->checksum;
    uint32_t calculated_checksum = calculate_checksum(config);
    printf("[+] flashmem_read_wifi_config: Checksum stored=0x%08x, calculated=0x%08x\n", stored_checksum, calculated_checksum);

    if (stored_checksum != calculated_checksum) {
        printf("[!] flashmem_read_wifi_config: Checksum mismatch (stored 0x%08x, calculated 0x%08x)\n", stored_checksum, calculated_checksum);
        return false;  // Data corrupted
    }

    // Copy SSID and password with bounds checking
    strncpy(ssid, config->ssid, ssid_size - 1);
    ssid[ssid_size - 1] = '\0';

    strncpy(password, config->password, password_size - 1);
    password[password_size - 1] = '\0';

    printf("[+] flashmem_read_wifi_config: Successfully read config - SSID: '%s'\n", ssid);
    return true;
}

// Write WiFi configuration to flash
bool flashmem_write_wifi_config(FlashMemContext *ctx, const char *ssid, const char *password) {
    printf("[+] flashmem_write_wifi_config: Writing config for SSID '%s'\n", ssid);

    wifi_config_t config;

    // Prepare configuration structure
    config.magic = FLASH_CONFIG_MAGIC;
    strncpy(config.ssid, ssid, sizeof(config.ssid) - 1);
    config.ssid[sizeof(config.ssid) - 1] = '\0';
    strncpy(config.password, password, sizeof(config.password) - 1);
    config.password[sizeof(config.password) - 1] = '\0';
    config.checksum = calculate_checksum(&config);

    printf("[+] flashmem_write_wifi_config: Prepared config (magic=0x%08x, checksum=0x%08x)\n",
           config.magic, config.checksum);

    // Disable interrupts during flash operation
    uint32_t ints = save_and_disable_interrupts();

    // Erase the flash sector
    printf("[+] flashmem_write_wifi_config: Erasing sector at offset 0x%08x\n", FLASH_TARGET_OFFSET);
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);

    // Write the configuration
    printf("[+] flashmem_write_wifi_config: Programming %d bytes at offset 0x%08x\n", sizeof(config), FLASH_TARGET_OFFSET);
    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t *)&config, sizeof(config));

    // Restore interrupts
    restore_interrupts(ints);

    printf("[+] flashmem_write_wifi_config: Flash operations completed\n");

    // Verify the write by reading back
    printf("[+] flashmem_write_wifi_config: Verifying write...\n");
    char verify_ssid[32];
    char verify_password[64];
    if (flashmem_read_wifi_config(ctx, verify_ssid, verify_password, sizeof(verify_ssid), sizeof(verify_password))) {
        if (strcmp(verify_ssid, ssid) == 0 && strcmp(verify_password, password) == 0) {
            printf("[+] flashmem_write_wifi_config: Write verification PASSED\n");
            return true;  // Write successful
        } else {
            printf("[!] flashmem_write_wifi_config: Write verification FAILED - data mismatch\n");
            printf("[!] Expected SSID: '%s', got: '%s'\n", ssid, verify_ssid);
            printf("[!] Expected Password: '%s', got: '%s'\n", password, verify_password);
        }
    } else {
        printf("[!] flashmem_write_wifi_config: Write verification FAILED - readback failed\n");
    }

    return false;  // Write failed
}

// Read station configuration from flash
bool flashmem_read_station_config(FlashMemContext *ctx, char *callsign, char *locator, size_t callsign_size, size_t locator_size) {
    // Validate parameters
    if (!ctx || !callsign || !locator || callsign_size == 0 || locator_size == 0) {
        printf("[!] flashmem_read_station_config: Invalid parameters\n");
        return false;
    }

    // Use XIP address for reading flash (following pico-examples pattern)
    uintptr_t xip_addr = XIP_BASE + (uintptr_t)ctx->_pFlashTargetOffset;
    printf("[+] flashmem_read_station_config: Reading from XIP address 0x%08x\n", (unsigned int)xip_addr);

    const wifi_config_t *config = (const wifi_config_t *)xip_addr;

    // Check if configuration exists and is valid
    uint32_t magic = config->magic;  // Read magic separately to avoid potential issues
    printf("[+] flashmem_read_station_config: Read magic 0x%08x\n", magic);

    if (magic != FLASH_CONFIG_MAGIC) {
        printf("[+] flashmem_read_station_config: No valid magic (found 0x%08x, expected 0x%08x)\n", magic, FLASH_CONFIG_MAGIC);
        return false;  // No valid configuration stored
    }

    // Verify checksum
    uint32_t stored_checksum = config->checksum;
    uint32_t calculated_checksum = calculate_checksum(config);
    printf("[+] flashmem_read_station_config: Checksum stored=0x%08x, calculated=0x%08x\n", stored_checksum, calculated_checksum);

    if (stored_checksum != calculated_checksum) {
        printf("[!] flashmem_read_station_config: Checksum mismatch (stored 0x%08x, calculated 0x%08x)\n", stored_checksum, calculated_checksum);
        return false;  // Data corrupted
    }

    // Copy callsign and locator with bounds checking
    strncpy(callsign, config->callsign, callsign_size - 1);
    callsign[callsign_size - 1] = '\0';

    strncpy(locator, config->locator, locator_size - 1);
    locator[locator_size - 1] = '\0';

    printf("[+] flashmem_read_station_config: Successfully read station config - Callsign: '%s', Locator: '%s'\n", callsign, locator);
    return true;
}

// Read frequency configuration from flash
bool flashmem_read_frequency_config(FlashMemContext *ctx, uint32_t *frequency) {
    // Validate parameters
    if (!ctx || !frequency) {
        printf("[!] flashmem_read_frequency_config: Invalid parameters\n");
        return false;
    }

    // Use XIP address for reading flash (following pico-examples pattern)
    uintptr_t xip_addr = XIP_BASE + (uintptr_t)ctx->_pFlashTargetOffset;
    printf("[+] flashmem_read_frequency_config: Reading from XIP address 0x%08x\n", (unsigned int)xip_addr);

    const wifi_config_t *config = (const wifi_config_t *)xip_addr;

    // Check if configuration exists and is valid
    uint32_t magic = config->magic;  // Read magic separately to avoid potential issues
    printf("[+] flashmem_read_frequency_config: Read magic 0x%08x\n", magic);

    if (magic != FLASH_CONFIG_MAGIC) {
        printf("[+] flashmem_read_frequency_config: No valid magic (found 0x%08x, expected 0x%08x)\n", magic, FLASH_CONFIG_MAGIC);
        return false;  // No valid configuration stored
    }

    // Verify checksum
    uint32_t stored_checksum = config->checksum;
    uint32_t calculated_checksum = calculate_checksum(config);
    printf("[+] flashmem_read_frequency_config: Checksum stored=0x%08x, calculated=0x%08x\n", stored_checksum, calculated_checksum);

    if (stored_checksum != calculated_checksum) {
        printf("[!] flashmem_read_frequency_config: Checksum mismatch (stored 0x%08x, calculated 0x%08x)\n", stored_checksum, calculated_checksum);
        return false;  // Data corrupted
    }

    // Read frequency
    *frequency = config->frequency;
    printf("[+] flashmem_read_frequency_config: Successfully read frequency: %lu Hz\n", *frequency);
    return true;
}

// Write WiFi and station configuration to flash
bool flashmem_write_config(FlashMemContext *ctx, const char *ssid, const char *password, const char *callsign, const char *locator, uint32_t frequency) {
    printf("[+] flashmem_write_config: Writing config for SSID '%s', Callsign '%s'\n", ssid, callsign);

    wifi_config_t config;

    // Prepare configuration structure
    config.magic = FLASH_CONFIG_MAGIC;
    strncpy(config.ssid, ssid, sizeof(config.ssid) - 1);
    config.ssid[sizeof(config.ssid) - 1] = '\0';
    strncpy(config.password, password, sizeof(config.password) - 1);
    config.password[sizeof(config.password) - 1] = '\0';
    strncpy(config.callsign, callsign, sizeof(config.callsign) - 1);
    config.callsign[sizeof(config.callsign) - 1] = '\0';
    strncpy(config.locator, locator, sizeof(config.locator) - 1);
    config.locator[sizeof(config.locator) - 1] = '\0';
    config.frequency = frequency;
    config.checksum = calculate_checksum(&config);

    printf("[+] flashmem_write_config: Prepared config (magic=0x%08x, checksum=0x%08x)\n",
           config.magic, config.checksum);

    // Disable interrupts during flash operation
    uint32_t ints = save_and_disable_interrupts();

    // Erase the flash sector
    printf("[+] flashmem_write_config: Erasing sector at offset 0x%08x\n", FLASH_TARGET_OFFSET);
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);

    // Write the configuration
    printf("[+] flashmem_write_config: Programming %d bytes at offset 0x%08x\n", sizeof(config), FLASH_TARGET_OFFSET);
    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t *)&config, sizeof(config));

    // Restore interrupts
    restore_interrupts(ints);

    printf("[+] flashmem_write_config: Flash operations completed\n");

    // Verify the write by reading back
    printf("[+] flashmem_write_config: Verifying write...\n");
    char verify_ssid[32], verify_password[64], verify_callsign[12], verify_locator[7];
    uint32_t verify_frequency;
    if (flashmem_read_wifi_config(ctx, verify_ssid, verify_password, sizeof(verify_ssid), sizeof(verify_password)) &&
        flashmem_read_station_config(ctx, verify_callsign, verify_locator, sizeof(verify_callsign), sizeof(verify_locator)) &&
        flashmem_read_frequency_config(ctx, &verify_frequency)) {
        if (strcmp(verify_ssid, ssid) == 0 && strcmp(verify_password, password) == 0 &&
            strcmp(verify_callsign, callsign) == 0 && strcmp(verify_locator, locator) == 0 &&
            verify_frequency == frequency) {
            printf("[+] flashmem_write_config: Write verification PASSED\n");
            return true;  // Write successful
        } else {
            printf("[!] flashmem_write_config: Write verification FAILED - data mismatch\n");
            printf("[!] Expected freq: %lu, got: %lu\n", frequency, verify_frequency);
        }
    } else {
        printf("[!] flashmem_write_config: Write verification FAILED - readback failed\n");
    }

    return false;  // Write failed
}

// Clear configuration (erase the sector)
void flashmem_clear_wifi_config(FlashMemContext *ctx) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}
