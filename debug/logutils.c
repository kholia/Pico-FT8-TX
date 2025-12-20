///////////////////////////////////////////////////////////////////////////////
//
//  Roman Piksaykin [piksaykin@gmail.com], R2BDY
//  https://www.qrz.com/db/r2bdy
//
///////////////////////////////////////////////////////////////////////////////
//
//
//  logutils.h - A set of utilities for logging/debugging.
//
//  DESCRIPTION
//      -
//
//  HOWTOSTART
//      -
//
//  PLATFORM
//      Raspberry Pi pico.
//
//  REVISION HISTORY
//      -
//
//  PROJECT PAGE
//      https://github.com/RPiks/pico-WSPR-tx
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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "hardware/clocks.h"
#include "hardware/powman.h"
#include "pico/stdlib.h"

// External NTP time variables (defined in main.c)
extern time_t ntp_time;
extern uint64_t powman_sync_ms;
extern bool have_ntp_time;

void StampPrintf(const char *pformat, ...) {
  static uint32_t sTick = 0;
  if (!sTick) {
    stdio_init_all();
  }

  stdio_set_driver_enabled(&stdio_uart, false);

  // Use NTP-synchronized time if available, otherwise use time since boot
  if (have_ntp_time) {
    uint64_t current_powman_ms = powman_timer_get_ms();
    uint64_t elapsed_ms = current_powman_ms - powman_sync_ms;
    time_t current_time = ntp_time + (elapsed_ms / 1000);
    struct tm *utc = gmtime(&current_time);

    // Debug: Print powman timer values occasionally
    static int debug_counter = 0;
    if (debug_counter++ % 100 == 0) {  // Every 100 calls
        printf("[DEBUG] powman_current=%llu, powman_sync=%llu, elapsed_ms=%llu\n",
               current_powman_ms, powman_sync_ms, elapsed_ms);
    }

    printf("%04d-%02d-%02d %02d:%02d:%02d.%03llu [NTP] ", utc->tm_year + 1900,
           utc->tm_mon + 1, utc->tm_mday, utc->tm_hour, utc->tm_min,
           utc->tm_sec, elapsed_ms % 1000);
  } else {
    uint64_t tm_us = to_us_since_boot(get_absolute_time());

    const uint32_t tm_day = (uint32_t)(tm_us / 86400000000ULL);
    tm_us -= (uint64_t)tm_day * 86400000000ULL;

    const uint32_t tm_hour = (uint32_t)(tm_us / 3600000000ULL);
    tm_us -= (uint64_t)tm_hour * 3600000000ULL;

    const uint32_t tm_min = (uint32_t)(tm_us / 60000000ULL);
    tm_us -= (uint64_t)tm_min * 60000000ULL;

    const uint32_t tm_sec = (uint32_t)(tm_us / 1000000ULL);
    tm_us -= (uint64_t)tm_sec * 1000000ULL;

    printf("%02lud%02lu:%02lu:%02lu.%06llu [BOOT] ", tm_day, tm_hour, tm_min,
           tm_sec, tm_us);
  }

  printf("[%04lu] ", sTick++);

  va_list argptr;
  va_start(argptr, pformat);
  vprintf(pformat, argptr);
  va_end(argptr);

  printf("\n");
  stdio_set_driver_enabled(&stdio_uart, true);
}
