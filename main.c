///////////////////////////////////////////////////////////////////////////////
//
//  Roman Piksaykin [piksaykin@gmail.com], R2BDY
//  https://www.qrz.com/db/r2bdy
//
///////////////////////////////////////////////////////////////////////////////
//
//
//  main.c - The project entry point.
//
//  DESCRIPTION
//      The pico-WSPR-tx project provides WSPR beacon function using only
//  Pi Pico board. *NO* additional hardware such as freq.synth required.
//  External GPS receiver is optional and serves a purpose of holding
//  WSPR time window order and accurate frequency drift compensation.
//
//  HOWTOSTART
//      ./build.sh; cp ./build/*.uf2 /media/Pico_Board/
//
//  PLATFORM
//      Raspberry Pi pico.
//
//  REVISION HISTORY
//      Rev 0.1   18 Nov 2023
//      Rev 0.5   02 Dec 2023
//              fork adding sync button
//
//  PROJECT PAGE
//      https://github.com/RPiks/pico-WSPR-tx
//
//  SUBMODULE PAGE
//      https://github.com/RPiks/pico-hf-oscillator
//
//  LICENCE
//      MIT License (http://www.opensource.org/licenses/mit-license.php)
//
//  Copyright (c) 2023 by Roman Piksaykin
//
//  Permission is hereby granted, free of charge,to any person obtaining a copy
//  of this software and associated documentation files (the Software), to deal
//  in the Software without restriction,including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY,WHETHER IN AN ACTION OF CONTRACT,TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.
///////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
#include "pico-hf-oscillator/defines.h"
#include "pico-hf-oscillator/lib/assert.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "wifi_setup.h"
#include <WSPRbeacon.h>
#include <defines.h>
#include <logutils.h>
#include <piodco.h>
#include <protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/powman.h" // RP2350 Power Manager for accurate timing
#include "hardware/watchdog.h"

#define CONFIG_GPS_SOLUTION_IS_MANDATORY NO
#define CONFIG_GPS_RELY_ON_PAST_SOLUTION NO
#define CONFIG_SCHEDULE_SKIP_SLOT_COUNT 5
// #define CONFIG_WSPR_DIAL_FREQUENCY 7040000UL //24926000UL // 28126000UL
// //7040000UL //18106000UL #define CONFIG_WSPR_DIAL_FREQUENCY 14078500UL
// //24926000UL // 28126000UL //7040000UL //18106000UL
#define CONFIG_WSPR_DIAL_FREQUENCY                                             \
  28074000UL       // 10m band FT8 frequency (28.074 MHz)
#define RF_PIN 6   // Pin 9 (GP6) on pico board
// #define REPEAT_TX_EVERY_MINUTE 4 // 4 is the minimum, for longer intervals
// choose 6,8,10,12, ...
#define REPEAT_TX_EVERY_MINUTE                                                 \
  1 // 4 is the minimum, for longer intervals choose 6,8,10,12, ...

WSPRbeaconContext *pWSPR;
PioDco DCO; /* Global DCO instance for oscillator */
wifi_setup_context_t wifi_ctx;

// NTP EXAMPLE

/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <time.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

typedef struct NTP_T_ {
  ip_addr_t ntp_server_address;
  bool dns_request_sent;
  struct udp_pcb *ntp_pcb;
  absolute_time_t ntp_test_time;
  alarm_id_t ntp_resend_alarm;
} NTP_T;

#define NTP_SERVER "time.cloudflare.com"
#define NTP_MSG_LEN 48
#define NTP_PORT 123
#define NTP_DELTA 2208988800 // seconds between 1 Jan 1900 and 1 Jan 1970
#define NTP_TEST_TIME (30 * 1000)
#define NTP_RESEND_TIME (10 * 1000)

int fetched_second = 0;
bool have_ntp_time = false;
time_t ntp_time = 0;         // Store the actual NTP time
uint64_t powman_sync_ms = 0; // POWMAN timer value when NTP was synced

// Called with results of operation
static void ntp_result(NTP_T *state, int status, time_t *result) {
  if (status == 0 && result) {
    struct tm *utc = gmtime(result);
    // datetime_t dt;  // Disabled for Pico 2 W

    printf("[+] Got NTP response: %02d/%02d/%04d %02d:%02d:%02d\n",
           utc->tm_mday, utc->tm_mon + 1, utc->tm_year + 1900, utc->tm_hour,
           utc->tm_min, utc->tm_sec);
    StampPrintf("NTP synchronized at %02d:%02d:%02d UTC", utc->tm_hour,
                utc->tm_min, utc->tm_sec);

    fetched_second = utc->tm_sec;
    ntp_time = *result;                     // Store the actual NTP time
    powman_sync_ms = powman_timer_get_ms(); // Record POWMAN timer at NTP sync
    sleep_us(1000);
    have_ntp_time = true;
  }

  if (state->ntp_resend_alarm > 0) {
    cancel_alarm(state->ntp_resend_alarm);
    state->ntp_resend_alarm = 0;
  }
  state->ntp_test_time = make_timeout_time_ms(NTP_TEST_TIME);
  state->dns_request_sent = false;
}

static int64_t ntp_failed_handler(alarm_id_t id, void *user_data);

// Make an NTP request
static void ntp_request(NTP_T *state) {
  // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure
  // correct locking. You can omit them if you are in a callback from lwIP. Note
  // that when using pico_cyw_arch_poll these calls are a no-op and can be
  // omitted, but it is a good practice to use them in case you switch the
  // cyw43_arch type later.
  cyw43_arch_lwip_begin();
  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
  uint8_t *req = (uint8_t *)p->payload;
  memset(req, 0, NTP_MSG_LEN);
  req[0] = 0x1b;
  udp_sendto(state->ntp_pcb, p, &state->ntp_server_address, NTP_PORT);
  pbuf_free(p);
  cyw43_arch_lwip_end();
}

static int64_t ntp_failed_handler(alarm_id_t id, void *user_data) {
  NTP_T *state = (NTP_T *)user_data;
  printf("[!] NTP request failed\n");
  ntp_result(state, -1, NULL);
  return 0;
}

// Call back with a DNS result
static void ntp_dns_found(const char *hostname, const ip_addr_t *ipaddr,
                          void *arg) {
  NTP_T *state = (NTP_T *)arg;
  if (ipaddr) {
    state->ntp_server_address = *ipaddr;
    printf("[+] NTP address is %s\n", ipaddr_ntoa(ipaddr));
    ntp_request(state);
  } else {
    printf("ntp dns request failed\n");
    ntp_result(state, -1, NULL);
  }
}

// NTP data received
static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                     const ip_addr_t *addr, u16_t port) {
  NTP_T *state = (NTP_T *)arg;
  uint8_t mode = pbuf_get_at(p, 0) & 0x7;
  uint8_t stratum = pbuf_get_at(p, 1);

  // Check the result
  if (ip_addr_cmp(addr, &state->ntp_server_address) && port == NTP_PORT &&
      p->tot_len == NTP_MSG_LEN && mode == 0x4 && stratum != 0) {
    uint8_t seconds_buf[4] = {0};
    pbuf_copy_partial(p, seconds_buf, sizeof(seconds_buf), 40);
    uint32_t seconds_since_1900 = seconds_buf[0] << 24 | seconds_buf[1] << 16 |
                                  seconds_buf[2] << 8 | seconds_buf[3];
    uint32_t seconds_since_1970 = seconds_since_1900 - NTP_DELTA;
    time_t epoch = seconds_since_1970;
    ntp_result(state, 0, &epoch);
  } else {
    printf("invalid ntp response\n");
    ntp_result(state, -1, NULL);
  }
  pbuf_free(p);
}

// Perform initialisation
static NTP_T *ntp_init(void) {
  NTP_T *state = (NTP_T *)calloc(1, sizeof(NTP_T));
  if (!state) {
    printf("failed to allocate state\n");
    return NULL;
  }
  state->ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
  if (!state->ntp_pcb) {
    printf("failed to create pcb\n");
    free(state);
    return NULL;
  }
  udp_recv(state->ntp_pcb, ntp_recv, state);
  return state;
}

void run_ntp_stuff(void) {
  NTP_T *state = ntp_init();
  if (!state)
    return;

  // Set alarm in case udp requests are lost
  state->ntp_resend_alarm =
      add_alarm_in_ms(NTP_RESEND_TIME, ntp_failed_handler, state, true);

  // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure
  // correct locking. You can omit them if you are in a callback from lwIP. Note
  // that when using pico_cyw_arch_poll these calls are a no-op and can be
  // omitted, but it is a good practice to use them in case you switch the
  // cyw43_arch type later.
  cyw43_arch_lwip_begin();
  int err = dns_gethostbyname(NTP_SERVER, &state->ntp_server_address,
                              ntp_dns_found, state);
  cyw43_arch_lwip_end();

  state->dns_request_sent = true;
  if (err == ERR_OK) {
    ntp_request(state);               // Cached result
  } else if (err != ERR_INPROGRESS) { // ERR_INPROGRESS means expect a callback
    printf("dns request failed\n");
    ntp_result(state, -1, NULL);
  }
#if PICO_CYW43_ARCH_POLL
  // if you are using pico_cyw43_arch_poll, then you must poll periodically from
  // your main loop (not from a timer interrupt) to check for Wi-Fi driver or
  // lwIP work that needs to be done.
  cyw43_arch_poll();
  // you can poll as often as you like, however if you have nothing else to do
  // you can choose to sleep until either a specified time, or cyw43_arch_poll()
  // has work to do:
  cyw43_arch_wait_for_work_until(
      state->dns_request_sent ? at_the_end_of_time : state->ntp_test_time);
#else
  // if you are not using pico_cyw43_arch_poll, then WiFI driver and lwIP work
  // is done via interrupt in the background. This sleep is just an example of
  // some (blocking) work you might be doing.
  sleep_ms(1000);
#endif
  free(state);
}

int main_ntp_with_credentials(const char *ssid, const char *password) {
  printf("[+] Connecting to WiFi: SSID='%s'\n", ssid);

  if (cyw43_arch_init()) {
    printf("[!] Failed to initialise WiFi\n");
    return 1;
  }

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); // safety first!

  cyw43_arch_enable_sta_mode();

  printf("[+] Attempting WiFi connection (max 3 retries)...\n");
  int retry_count = 0;
  const int max_retries = 3;

  while (retry_count < max_retries) {
    printf("[+] Connection attempt %d/%d...\n", retry_count + 1, max_retries);
    int result = cyw43_arch_wifi_connect_timeout_ms(
        ssid, password, CYW43_AUTH_WPA2_AES_PSK, 15000);
    printf("[+] cyw43_arch_wifi_connect_timeout_ms returned: %d\n", result);

    if (result == 0) {
      printf("[+] WiFi connected successfully!\n");
      break;
    } else {
      retry_count++;
      printf("[!] WiFi connection failed (attempt %d/%d), error code: %d\n",
             retry_count, max_retries, result);
      if (retry_count < max_retries) {
        printf("[+] Waiting 2 seconds before retry...\n");
        sleep_ms(2000);
      }
    }
  }

  if (retry_count >= max_retries) {
    printf("[!] ❌ Failed to connect to WiFi after %d attempts\n", max_retries);
    printf("[!] Possible issues:\n");
    printf("[!]   - Wrong password for SSID '%s'\n", ssid);
    printf("[!]   - WiFi network '%s' not available\n", ssid);
    printf("[!]   - Wrong security type (assuming WPA2-AES)\n");
    return 2; // Connection failed
  }

  // Disable Wi-Fi power management
  cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);

  printf("[+] Running NTP synchronization...\n");
  run_ntp_stuff();

  cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);

  return 0;
}

// Legacy WIFI_SSID macros removed - now using stored credentials only

// NTP EXAMPLE ENDS

int main() {
  // Initialize serial output FIRST before any printf calls
  stdio_init_all();

  printf("[+] Pico FT8 Transmitter starting...\n");

  InitPicoHW();
  printf("[+] Hardware initialized\n");

  // Initialize RP2350 Power Manager timer for accurate timing
  powman_timer_set_1khz_tick_source_xosc();
  powman_timer_start();
  printf("[+] POWMAN timer initialized\n");

  sleep_ms(5000);

  // Initialize WiFi setup system
  wifi_setup_context_t wifi_ctx;
  if (!wifi_setup_init(&wifi_ctx)) {
    printf("[!] WiFi setup init failed\n");
    return 1;
  }
  printf("[+] WiFi setup initialized\n");

  // Check if WiFi credentials are already stored
  printf("[+] Checking for stored WiFi credentials on boot...\n");
  bool has_creds = wifi_setup_has_credentials(&wifi_ctx);
  printf("[+] wifi_setup_has_credentials() returned: %s\n",
         has_creds ? "true" : "false");

  if (has_creds) {
    printf("[+] Found stored WiFi credentials!\n");

    // Get the stored credentials
    char stored_ssid[32] = {0};
    char stored_password[64] = {0};
    printf("[+] Attempting to read stored credentials...\n");

    bool read_success = wifi_setup_get_credentials(
        &wifi_ctx, stored_ssid, stored_password, sizeof(stored_ssid),
        sizeof(stored_password));
    printf("[+] wifi_setup_get_credentials() returned: %s\n",
           read_success ? "true" : "false");

    // Read station configuration (callsign and locator)
    char stored_callsign[12] = {0};
    char stored_locator[7] = {0};
    bool station_read_success = wifi_setup_get_station_config(
        &wifi_ctx, stored_callsign, stored_locator, sizeof(stored_callsign),
        sizeof(stored_locator));
    printf("[+] wifi_setup_get_station_config() returned: %s\n",
           station_read_success ? "true" : "false");

    // Read frequency configuration
    uint32_t stored_frequency = 28074000UL; // Default 10m
    bool freq_read_success =
        wifi_setup_get_frequency_config(&wifi_ctx, &stored_frequency);
    printf("[+] wifi_setup_get_frequency_config() returned: %s, frequency: %lu "
           "Hz\n",
           freq_read_success ? "true" : "false", stored_frequency);

    if (read_success && strlen(stored_ssid) > 0 &&
        strlen(stored_password) > 0 && station_read_success &&
        strlen(stored_callsign) > 0 && strlen(stored_locator) > 0) {
      printf("[+] Read credentials - SSID: '%s', Password: '%s'\n", stored_ssid,
             stored_password);
      printf("[+] Connecting to WiFi network: '%s'\n", stored_ssid);

      // Connect to WiFi and sync NTP time
      printf("[+] Attempting WiFi connection to '%s'...\n", stored_ssid);
      int connect_result =
          main_ntp_with_credentials(stored_ssid, stored_password);
      printf("[+] main_ntp_with_credentials() returned: %d\n", connect_result);

      if (connect_result == 0) {
        printf("[+] ==========================================\n");
        printf("[+]   WiFi Connected! NTP Synced!\n");
        printf("[+]   Starting FT8 Transmission Mode!\n");
        printf("[+] ==========================================\n");

        // Initialize FT8 transmission system (following kholia's working
        // implementation)
        printf("[+] Initializing FT8 transmission system...\n");

        // Initialize the Direct Digital Oscillator (DCO) for frequency
        // generation
        const uint32_t clkhz = PLL_SYS_MHZ * MHz;
        int init_result = PioDCOInit(&DCO, RF_PIN, clkhz);
        if (init_result != 0) {
          printf("[!] Failed to initialize DCO (error: %d)\n", init_result);
          return 1;
        }
        printf("[+] DCO initialized successfully\n");

        // Initialize WSPR/FT8 beacon context with configured callsign and
        // locator
        pWSPR = WSPRbeaconInit(stored_callsign, stored_locator,
                               20, // 20dBm TX power
                               &DCO, stored_frequency + 1500UL, 0, RF_PIN);
        if (!pWSPR) {
          printf("[!] Failed to initialize FT8 beacon context\n");
          return 1;
        }
        printf("[+] FT8 beacon context initialized\n");
        printf("[+] Callsign: %s\n", stored_callsign);
        printf("[+] Locator: %s\n", stored_locator);
        printf("[+] Dial Frequency: %lu Hz\n", CONFIG_WSPR_DIAL_FREQUENCY);

        // Launch the oscillator on core 1
        printf("[+] Launching oscillator on core 1...\n");
        multicore_launch_core1(Core1Entry);
        printf("[+] Oscillator core launched\n");

        printf("[+] ==========================================\n");
        printf("[+]   FT8 TRANSMISSION ACTIVE!\n");
        printf("[+]   Broadcasting on %lu Hz\n", stored_frequency);
        printf("[+]   Callsign: %s\n", stored_callsign);
        printf("[+]   Locator: %s\n", stored_locator);
        printf("[+] ==========================================\n");

        // Main transmission loop (using powman timer for timing)
        while (1) {
          // Get current time using powman timer + NTP offset
          uint64_t current_powman_ms = powman_timer_get_ms();
          uint64_t elapsed_ms = current_powman_ms - powman_sync_ms;
          time_t current_time = ntp_time + (elapsed_ms / 1000);
          struct tm *utc = gmtime(&current_time);

          // Check for FT8 transmission slots (every 15 seconds: 0, 15, 30, 45)
          // Use a range check to catch the slot timing accurately
          int current_second = utc->tm_sec;
          bool is_slot_time = false;
          static bool tx_triggered_this_slot = false;

          // Check if we're in a transmission slot (0, 15, 30, 45 seconds)
          if (current_second % 15 == 0) {
            is_slot_time = true;
          }

          if (is_slot_time && !tx_triggered_this_slot) {
            printf("[+] FT8 transmission slot reached at %02d:%02d:%02d! "
                   "Starting TX...\n",
                   utc->tm_hour, utc->tm_min, utc->tm_sec);
            tx_triggered_this_slot = true;

            // Start the oscillator
            PioDCOStart(&DCO);

            // Create and send FT8 packet
            WSPRbeaconCreatePacket(pWSPR);
            sleep_ms(100);
            WSPRbeaconSendPacket(pWSPR);

            // Wait for transmission to complete (with timeout and safety measures)
            printf("[+] Waiting for transmission to complete...\n");
            bool tx_complete = false;
            uint32_t tx_start_time = powman_timer_get_ms();
            const uint32_t TX_TIMEOUT_MS = 15000; // 15 second timeout

            while (!tx_complete && (powman_timer_get_ms() - tx_start_time) < TX_TIMEOUT_MS) {
              if (!TxChannelPending(pWSPR->_pTX)) {
                // Add a small delay to ensure transmission is fully complete
                sleep_ms(50);
                PioDCOStop(&DCO);
                // Explicitly ensure RF pin is LOW after stopping DCO
                gpio_put(RF_PIN, 0);
                printf("[+] FT8 transmission completed!\n");
                tx_complete = true;
              }
              sleep_ms(100);
            }

            // Safety fallback: if transmission didn't complete normally, stop DCO anyway
            if (!tx_complete) {
              printf("[!] Transmission timeout - forcing DCO stop\n");
              PioDCOStop(&DCO);
              gpio_put(RF_PIN, 0);
            }
          }

          sleep_ms(100);

          // Final safety check: ensure RF pin is always LOW when not transmitting
          gpio_put(RF_PIN, 0);

          // Reset the trigger flag when we move to a new 15-second window
          // This ensures we can trigger again for the next slot
          static int last_slot_window = -1;
          int current_slot_window = current_second / 15;
          if (current_slot_window != last_slot_window) {
            tx_triggered_this_slot = false;
            last_slot_window = current_slot_window;
          }
        }

        // This should never be reached
        printf("[+] FT8 transmission ended unexpectedly\n");
        return 0;
      } else {
        printf("[!] Failed to connect to stored WiFi - starting AP mode for "
               "reconfiguration\n");
        goto start_ap_mode;
      }
    } else {
      printf("[!] Failed to read valid stored credentials - starting AP mode "
             "for reconfiguration\n");
      printf("[!] Read SSID: '%s', Password length: %d\n", stored_ssid,
             strlen(stored_password));
      goto start_ap_mode;
    }
  } else {
    printf("[+] No stored credentials found - starting fresh AP mode setup\n");
    goto start_ap_mode;
  }

  // Should never reach here - FT8 transmission runs forever
  return 0;

start_ap_mode:
  // WiFi setup system - fresh initialization for AP mode
  printf("[+] Initializing WiFi setup system for AP mode...\n");
  wifi_setup_context_t ap_wifi_ctx; // Use separate context for AP mode
  if (!wifi_setup_init(&ap_wifi_ctx)) {
    printf("[!] WiFi setup init failed\n");
    return 1;
  }
  printf("[+] WiFi setup initialized successfully\n");

  // Start AP mode for WiFi setup
  printf("[+] Starting AP mode...\n");
  if (!wifi_setup_start_ap(&ap_wifi_ctx)) {
    printf("[!] Failed to start AP mode\n");
    return 1;
  }

  printf("[+] ==========================================\n");
  printf("[+]   Pico FT8 WiFi Setup Mode Active!\n");
  printf("[+] ==========================================\n");
  printf("[+] AP Network: Pico-FT8-Setup\n");
  printf("[+] Password:   password\n");
  printf("[+] IP Address: 192.168.1.1\n");
  printf("[+] Web Interface: http://192.168.1.1\n");
  printf("[+] \n");
  printf("[+] Configure: WiFi + Callsign + Locator\n");
  printf("[+] ==========================================\n");

  // Simple AP mode loop - wait for WiFi setup
  printf("[+] Entering AP mode setup loop...\n");
  while (!wifi_setup_is_setup_complete()) {
    sleep_ms(1000);
    printf("[+] Waiting for WiFi setup...\n");

    // Double-check completion status
    if (wifi_setup_is_setup_complete()) {
      printf("[+] Setup completion detected in loop!\n");
      break;
    }
  }

  printf("[+] WiFi setup complete! Rebooting in 2 seconds...\n");
  sleep_ms(2000);
  printf("[+] Rebooting now!\n");
  watchdog_reboot(0, SRAM_END, 10);

  return 0;
}
