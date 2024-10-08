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

#include "pico.h"
#include "pico/util/datetime.h"
#include "hardware/adc.h"
// #include "hardware/rtc.h"  // https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#group_hardware_rtc

#define CONFIG_GPS_SOLUTION_IS_MANDATORY NO
#define CONFIG_GPS_RELY_ON_PAST_SOLUTION NO
#define CONFIG_SCHEDULE_SKIP_SLOT_COUNT 5
// #define CONFIG_WSPR_DIAL_FREQUENCY 7040000UL //24926000UL // 28126000UL //7040000UL //18106000UL
// #define CONFIG_WSPR_DIAL_FREQUENCY 14078500UL //24926000UL // 28126000UL //7040000UL //18106000UL
#define CONFIG_WSPR_DIAL_FREQUENCY 28075500UL  //24926000UL // 28126000UL //7040000UL //18106000UL
#define CONFIG_CALLSIGN "YOURCALL"             // NOT USED
#define CONFIG_LOCATOR4 "YOURLOCATOR"          // NOT USED
#define BTN_PIN 16                             // Pin 21 on pico board
// #define REPEAT_TX_EVERY_MINUTE 4 // 4 is the minimum, for longer intervals choose 6,8,10,12, ...
#define REPEAT_TX_EVERY_MINUTE 1  // 4 is the minimum, for longer intervals choose 6,8,10,12, ...

WSPRbeaconContext *pWSPR;

int main() {
  StampPrintf("\n");
  // sleep_ms(5000);
  StampPrintf("R2BDY and VU3CER Pico-FT8-TX start.");

  adc_init();
  adc_set_temp_sensor_enabled(true);

  gpio_init(BTN_PIN);
  gpio_set_dir(BTN_PIN, GPIO_IN);
  gpio_pull_up(BTN_PIN);

  InitPicoHW();

  PioDco DCO = { 0 };

  StampPrintf("FT8 beacon init...");

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

  DCO._pGPStime = GPStimeInit(0, 9600, GPS_PPS_PIN);
  assert_(DCO._pGPStime);
  StampPrintf("When button pressed, I start transmitting.");
  int tick = 0;

  while (1) {
    bool txStarted = 0;
    while ((!gpio_get(BTN_PIN))) {
      StampPrintf("Start fsk'ing!");
      StampPrintf("Not supporting GPS solution, start tx now.");
      PioDCOStart(pWB->_pTX->_p_oscillator);
      WSPRbeaconCreatePacket(pWB);
      sleep_ms(100);
      WSPRbeaconSendPacket(pWB);
      StampPrintf("The system will wait for next trigger when tx is completed.");
      bool wait4endTX = 0;
      while (!wait4endTX) {
        if (!TxChannelPending(pWB->_pTX)) {
          PioDCOStop(pWB->_pTX->_p_oscillator);
          StampPrintf("System halted.");
          wait4endTX = 1;
        }
      }
    }
  }
  sleep_ms(10);
}
