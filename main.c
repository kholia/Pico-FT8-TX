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
//  WSPR time window order and accurate frequancy drift compensation.
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
#include <WSPRbeacon.h>
#include <defines.h>
#include <logutils.h>
#include <piodco.h>
#include <protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/rtc.h"
#include "hardware/watchdog.h"
#include "pico/util/datetime.h"

#define CONFIG_GPS_SOLUTION_IS_MANDATORY NO
#define CONFIG_GPS_RELY_ON_PAST_SOLUTION NO
#define CONFIG_SCHEDULE_SKIP_SLOT_COUNT 5
// #define CONFIG_WSPR_DIAL_FREQUENCY 7040000UL //24926000UL // 28126000UL
// //7040000UL //18106000UL #define CONFIG_WSPR_DIAL_FREQUENCY 14078500UL
// //24926000UL // 28126000UL //7040000UL //18106000UL
#define CONFIG_WSPR_DIAL_FREQUENCY                                             \
  28075500UL // 24926000UL // 28126000UL //7040000UL //18106000UL
#define CONFIG_CALLSIGN "YOURCALL"    // NOT USED
#define CONFIG_LOCATOR4 "YOURLOCATOR" // NOT USED
#define BTN_PIN 16                    // Pin 21 on pico board
// #define REPEAT_TX_EVERY_MINUTE 4 // 4 is the minimum, for longer intervals
// choose 6,8,10,12, ...
#define REPEAT_TX_EVERY_MINUTE                                                 \
  1 // 4 is the minimum, for longer intervals choose 6,8,10,12, ...

WSPRbeaconContext *pWSPR;

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

// Called with results of operation
static void ntp_result(NTP_T *state, int status, time_t *result) {
  if (status == 0 && result) {
    struct tm *utc = gmtime(result);
    datetime_t dt;

    printf("[+] Got NTP response: %02d/%02d/%04d %02d:%02d:%02d\n",
           utc->tm_mday, utc->tm_mon + 1, utc->tm_year + 1900, utc->tm_hour,
           utc->tm_min, utc->tm_sec);

    fetched_second = utc->tm_sec;
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

int main_ntp() {
  if (cyw43_arch_init()) {
    printf("[!] Failed to initialise\n");
    return 1;
  }

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); // safety first!

  cyw43_arch_enable_sta_mode();

  while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                            CYW43_AUTH_WPA2_AES_PSK, 10000)) {
    printf("[!] WiFi failed to connect\n");
  }

  // Disable Wi-Fi power management
  cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);

  run_ntp_stuff();

  cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);

  return 0;
}

// NTP EXAMPLE ENDS

int main() {
  adc_init();
  adc_set_temp_sensor_enabled(true);

  InitPicoHW();

  stdio_init_all();

  rtc_init();

  main_ntp();

  char datetime_buf[256];
  char *datetime_str = &datetime_buf[0];

  // Start on Friday 5th of June 2020 15:45:00
  datetime_t t = {.year = 2020,
                  .month = 06,
                  .day = 05,
                  .dotw = 5, // 0 is Sunday, so 5 is Friday
                  .hour = 0,
                  .min = 05,
                  .sec = fetched_second};

  // Start the RTC
  rtc_init();
  rtc_set_datetime(&t);
  sleep_us(64);

  /* while (true) {
    rtc_get_datetime(&t);
    datetime_to_str(datetime_str, sizeof(datetime_buf), &t);
    printf("\r%s      ", datetime_str);
    sleep_ms(100);
  } */

  PioDco DCO = {0};

  StampPrintf("FT8 beacon init...");

  if (watchdog_caused_reboot()) {
    printf("Rebooted by Watchdog!\n");
  } else {
    printf("Clean boot\n");
  }

  WSPRbeaconContext *pWB = WSPRbeaconInit(
      CONFIG_CALLSIGN, /* the Callsign. */
      CONFIG_LOCATOR4, /* the default QTH locator if GPS isn't used. */
      12,              /* Tx power, dbm. */
      &DCO,            /* the PioDCO object. */
      CONFIG_WSPR_DIAL_FREQUENCY,
      55UL,     /* the carrier freq. shift relative to dial freq. */
      RFOUT_PIN /* RF output GPIO pin. */
  );
  assert_(pWB);
  pWSPR = pWB;

  pWB->_txSched._u8_tx_GPS_mandatory = CONFIG_GPS_SOLUTION_IS_MANDATORY;
  pWB->_txSched._u8_tx_GPS_past_time = CONFIG_GPS_RELY_ON_PAST_SOLUTION;
  pWB->_txSched._u8_tx_slot_skip = CONFIG_SCHEDULE_SKIP_SLOT_COUNT;

  multicore_launch_core1(Core1Entry);
  StampPrintf("RF oscillator started.");

  rtc_set_datetime(&t);

  if (have_ntp_time) {
    while (1) {
      int ret = rtc_get_datetime(&t);
      sleep_us(64);
      watchdog_update();
      // printf("%d\n", t.sec);
      // if ((t.sec % 15) == 0) {
      if (t.sec == 15) {
        StampPrintf("Start fsk'ing!");
        PioDCOStart(pWB->_pTX->_p_oscillator);
        WSPRbeaconCreatePacket(pWB);
        sleep_ms(100);
        WSPRbeaconSendPacket(pWB);
        bool wait4endTX = 0;
        // StampPrintf("Not supporting GPS solution, start tx now 2.");
        while (!wait4endTX) {
          if (!TxChannelPending(pWB->_pTX)) {
            PioDCOStop(pWB->_pTX->_p_oscillator);
            StampPrintf("TX halted...");
            wait4endTX = 1;
          }
          watchdog_update();
          cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
          sleep_ms(500);
          cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
          watchdog_update();
        }
        // ATTENTION: The RFOUT_PIN can be left HIGH at this point. This is
        // quite problematic when external amplifier is connected!
        //
        // XXX: Better to reboot here?
        watchdog_reboot(0, SRAM_END, 10);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(100);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
      }
      sleep_ms(100);
    }
  } else {
    while (1) {
      printf("Oops!\n");
      watchdog_update();
      cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
      sleep_ms(2000);
      watchdog_reboot(0, SRAM_END, 10);
    }
  }
}
