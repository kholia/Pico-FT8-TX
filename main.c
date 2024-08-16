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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico-hf-oscillator/lib/assert.h"
#include "pico-hf-oscillator/defines.h"
#include <defines.h>
#include <piodco.h>
#include <WSPRbeacon.h>
#include <logutils.h>
#include <protos.h>

#include "hardware/rtc.h"
#include "pico/util/datetime.h"

#define CONFIG_GPS_SOLUTION_IS_MANDATORY NO
#define CONFIG_GPS_RELY_ON_PAST_SOLUTION NO
#define CONFIG_SCHEDULE_SKIP_SLOT_COUNT 5
// #define CONFIG_WSPR_DIAL_FREQUENCY 7040000UL //24926000UL // 28126000UL //7040000UL //18106000UL
#define CONFIG_WSPR_DIAL_FREQUENCY 14078500UL //24926000UL // 28126000UL //7040000UL //18106000UL
#define CONFIG_CALLSIGN "YOURCALL" // NOT USED
#define CONFIG_LOCATOR4 "YOURLOCATOR" // NOT USED
#define BTN_PIN 21 //pin 27 on pico board
#define REPEAT_TX_EVERY_MINUTE 4 // 4 is the minimum, for longer intervals choose 6,8,10,12, ...

WSPRbeaconContext *pWSPR;

// CAT code
static uint8_t insideCat = 0;
unsigned char doingCAT = 0;
bool txCAT             = false;  // turned on if the transmitting due to a CAT command
bool message_available = false;
#define CAT_MAX 24
static char cat[CAT_MAX + 64];   // 64 avoids overflow
uint16_t offset = 1200;
static int sidx = 0;
volatile char current_message[32];
char buffer[320];
unsigned char rec_byte[2];
unsigned int msg_index;

int main()
{
  StampPrintf("\n");
  StampPrintf("R2BDY and VU3CER Pico-FT8-TX start.");

  gpio_init(BTN_PIN);
  gpio_set_dir(BTN_PIN, GPIO_IN);

  InitPicoHW();

  PioDco DCO = {0};

  StampPrintf("FT8 beacon init...");

  WSPRbeaconContext *pWB = WSPRbeaconInit(
                             CONFIG_CALLSIGN,/* the Callsign. */
                             CONFIG_LOCATOR4,/* the default QTH locator if GPS isn't used. */
                             12,             /* Tx power, dbm. */
                             &DCO,           /* the PioDCO object. */
                             CONFIG_WSPR_DIAL_FREQUENCY,
                             55UL,           /* the carrier freq. shift relative to dial freq. */
                             RFOUT_PIN       /* RF output GPIO pin. */
                           );
  assert_(pWB);
  pWSPR = pWB;

  pWB->_txSched._u8_tx_GPS_mandatory  = CONFIG_GPS_SOLUTION_IS_MANDATORY;
  pWB->_txSched._u8_tx_GPS_past_time  = CONFIG_GPS_RELY_ON_PAST_SOLUTION;
  pWB->_txSched._u8_tx_slot_skip      = CONFIG_SCHEDULE_SKIP_SLOT_COUNT;

  multicore_launch_core1(Core1Entry);
  StampPrintf("RF oscillator started.");

  DCO._pGPStime = GPStimeInit(0, 9600, GPS_PPS_PIN);
  assert_(DCO._pGPStime);
  StampPrintf("When button pressed, I start transmitting.");
  int tick = 0;
  rtc_init();
  datetime_t t = {
    .year  = 2024,
    .month = 01,
    .day   = 01,
    .dotw  = 1, // 0 is Sunday, so 5 is Friday
    .hour  = 1,
    .min   = REPEAT_TX_EVERY_MINUTE,
    .sec   = 00
  };
  while (1)
  {
    // CAT
    for (;;) {
      msg_index = 0;
      int cmd = getchar_timeout_us(0);
      if (cmd == PICO_ERROR_TIMEOUT) {
        break;
      } else {
        if (cmd == 'm') {
          msg_index = 0;
          cmd = 0;
          printf("m");
          while (msg_index < sizeof(current_message)) {
            int c = getchar_timeout_us(0);
            if (c == PICO_ERROR_TIMEOUT)
              break;
            else {
              current_message[msg_index++] = (c & 0xFF);
              current_message[msg_index] = 0; // terminate string
              message_available = true;
              // printf("%s\n", current_message);
            }
          }
        }
        if (cmd == 'o')
        {
          msg_index = 0;
          // Offset encoded in two bytes
          while (msg_index < 2)
          {
            char c = getchar_timeout_us(0);
            if (c == PICO_ERROR_TIMEOUT)
              break;
            rec_byte[msg_index] = c;
            msg_index++;
          }
          offset = rec_byte[0] + (rec_byte[1] << 8);
          printf("o");
        }
        if (cmd == 't')
        {
          StampPrintf("Start TX'ing now...");
          PioDCOStart(pWB->_pTX->_p_oscillator);
          WSPRbeaconCreatePacket(pWB);
          sleep_ms(100);
          WSPRbeaconSendPacket(pWB);
          bool wait4endTX = 0;
          while (!wait4endTX)
          {
            if (!TxChannelPending(pWB->_pTX))
            {
              PioDCOStop(pWB->_pTX->_p_oscillator);
              wait4endTX = 1;
            }
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
            sleep_ms(500);
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
            // sleep_ms(500);
          }
        }
      }
    }

    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(100);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    sleep_ms(100);
  }
  sleep_ms(100);
}
