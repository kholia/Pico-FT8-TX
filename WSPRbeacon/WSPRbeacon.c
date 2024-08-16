///////////////////////////////////////////////////////////////////////////////
//
//  Roman Piksaykin [piksaykin@gmail.com], R2BDY
//  https://www.qrz.com/db/r2bdy
//
///////////////////////////////////////////////////////////////////////////////
//
//
//  WSPRbeacon.c - WSPR beacon - related functions.
// 
//  DESCRIPTION
//      The pico-WSPR-tx project provides WSPR beacon function using only
//  Pi Pico board. *NO* additional hardware such as freq.synth required.
//
//  HOWTOSTART
//  .
//
//  PLATFORM
//      Raspberry Pi pico.
//
//  REVISION HISTORY
// 
//      Rev 0.1   18 Nov 2023
//  Initial release.
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
#include "WSPRbeacon.h"
#include <WSPRutility.h>
#include <maidenhead.h>

// An FT8 signal starts 0.5 seconds into a cycle and lasts 12.64 seconds. It
// consists of 79 symbols, each 0.16 seconds long. Each symbol is a single
// steady tone. For any given signal there are eight possible tones. The tone
// spacing is 6.25 Hz. FT8 symbol period = 1920 / 12000 seconds = 160.

// WSPR:
// Tone separation: 1.4648 Hz (total = 5.8592 Hz)
// Number of symbols: 162
// Keying rate: 12000/8192 = 1.46484375 baud
// Duration of transmission: 162 x 8192/12000 = 110.592s
// Wait time: 9,408 (9408000us)
// Symbol duration: 0.68266667s (682667us)

/// @brief Initializes a new WSPR beacon context.
/// @param pcallsign HAM radio callsign, 12 chr max.
/// @param pgridsquare Maidenhead locator, 7 chr max.
/// @param txpow_dbm TX power, db`mW.
/// @param pdco Ptr to working DCO.
/// @param dial_freq_hz The begin of working WSPR passband.
/// @param shift_freq_hz The shift of tx freq. relative to dial_freq_hz.
/// @param gpio Pico's GPIO pin of RF output.
/// @return Ptr to the new context.
WSPRbeaconContext *WSPRbeaconInit(const char *pcallsign, const char *pgridsquare, int txpow_dbm,
                                  PioDco *pdco, uint32_t dial_freq_hz, uint32_t shift_freq_hz,
                                  int gpio)
{
    assert_(pcallsign);
    assert_(pgridsquare);
    assert_(pdco);

    WSPRbeaconContext *p = calloc(1, sizeof(WSPRbeaconContext));
    assert_(p);

    strncpy(p->_pu8_callsign, pcallsign, sizeof(p->_pu8_callsign));
    strncpy(p->_pu8_locator, pgridsquare, sizeof(p->_pu8_locator));
    p->_u8_txpower = txpow_dbm;

    // http://squirrelengineering.com/high-altitude-balloon/adrift-problem-solving-fs2-wspr-drift/
    // p->_pTX = TxChannelInit(682667, 0, pdco); // WSPR_DELAY is 683
    p->_pTX = TxChannelInit(159000, 0, pdco); // FT8_DELAY is 159
    assert_(p->_pTX);
    p->_pTX->_u32_dialfreqhz = dial_freq_hz + shift_freq_hz;
    p->_pTX->_i_tx_gpio = gpio;

    return p;
}

/// @brief Sets dial (baseband minima) freq.
/// @param pctx Context.
/// @param freq_hz the freq., Hz.
void WSPRbeaconSetDialFreq(WSPRbeaconContext *pctx, uint32_t freq_hz)
{
    assert_(pctx);
    pctx->_pTX->_u32_dialfreqhz = freq_hz;
}

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>

#include "common/common.h"
#include "ft8/message.h"
#include "ft8/encode.h"
#include "ft8/constants.h"

#define LOG_LEVEL LOG_INFO
#include "ft8/debug.h"

void packtext77(const char* text, uint8_t* b77);

extern char current_message[32];

void ft8_encode_top(uint8_t *tones)
{
    // char *message = "WQ6WW1HDK1TE"; // ATTN: You will want to customize this message!
    char *message = current_message;
    // char *message = "CQ K1TE FN42";
    // First, pack the text data into binary message
    ftx_message_t msg;
    ftx_message_rc_t rc = ftx_message_encode(&msg, NULL, message);
    if (rc != FTX_MESSAGE_RC_OK)
    {
        // Try 'free text' encoding
        if (strlen(message) <= 13)
            packtext77(message, (uint8_t *)&msg.payload);
        else {
            printf("Cannot parse message!\n");
            printf("RC = %d\n", (int)rc);
       }
    }

    printf("Packed data: ");
    for (int j = 0; j < 10; ++j)
    {
        printf("%02x ", msg.payload[j]);
    }
    printf("\n");

    int num_tones = FT8_NN;

    ft8_encode(msg.payload, tones);

    printf("FSK tones: ");
    for (int j = 0; j < num_tones; ++j)
    {
        printf("%d", tones[j]);
    }
    printf("\n");
}

/// @brief Constructs a new WSPR packet using the data available.
/// @param pctx Context
/// @return 0 if OK.
int WSPRbeaconCreatePacket(WSPRbeaconContext *pctx)
{
    assert_(pctx);

    // wspr_encode(pctx->_pu8_callsign, pctx->_pu8_locator, pctx->_u8_txpower, pctx->_pu8_outbuf);

    // FT8 hack
    ft8_encode_top(pctx->_pu8_outbuf);

    return 0;
}

#define FT8_SYMBOL_COUNT 79

/// @brief Sends a prepared WSPR packet using TxChannel.
/// @param pctx Context.
/// @return 0, if OK.
int WSPRbeaconSendPacket(const WSPRbeaconContext *pctx)
{
    assert_(pctx);
    assert_(pctx->_pTX);
    assert_(pctx->_pTX->_u32_dialfreqhz > 500 * kHz);

    TxChannelClear(pctx->_pTX);

    // memcpy(pctx->_pTX->_pbyte_buffer, pctx->_pu8_outbuf, WSPR_SYMBOL_COUNT);
    // pctx->_pTX->_ix_input = WSPR_SYMBOL_COUNT;

    memcpy(pctx->_pTX->_pbyte_buffer, pctx->_pu8_outbuf, FT8_SYMBOL_COUNT);
    pctx->_pTX->_ix_input = FT8_SYMBOL_COUNT;

    return 0;
}

/// @brief Arranges WSPR sending in accordance with pre-defined schedule.
/// @brief It works only if GPS receiver available (for now).
/// @param pctx Ptr to Context.
/// @param verbose Whether stdio output is needed.
/// @return 0 if OK, -1 if NO GPS received available
int WSPRbeaconTxScheduler(WSPRbeaconContext *pctx, int verbose)
{
    assert_(pctx);

    const uint64_t u64tmnow = GetUptime64();
    const uint32_t is_GPS_available = pctx->_pTX->_p_oscillator->_pGPStime->_time_data._u32_nmea_gprmc_count;
    const uint32_t is_GPS_active = pctx->_pTX->_p_oscillator->_pGPStime->_time_data._u8_is_solution_active;
    const uint32_t is_GPS_override = pctx->_txSched._u8_tx_GPS_past_time == YES;

    const uint64_t u64_GPS_last_age_sec 
        = (u64tmnow - pctx->_pTX->_p_oscillator->_pGPStime->_time_data._u64_sysclk_nmea_last) / 1000000ULL;

    if(!is_GPS_available)
    {
        if(verbose) StampPrintf("WSPR> Waiting for GPS receiver...");
        return -1;
    }

    if(is_GPS_active || (pctx->_pTX->_p_oscillator->_pGPStime->_time_data._u32_utime_nmea_last &&
                         is_GPS_override && u64_GPS_last_age_sec < WSPR_MAX_GPS_DISCONNECT_TM))
    {
        const uint32_t u32_unixtime_now 
            = pctx->_pTX->_p_oscillator->_pGPStime->_time_data._u32_utime_nmea_last + u64_GPS_last_age_sec;
        
        const int isec_of_hour = u32_unixtime_now % HOUR;
        const int islot_number = isec_of_hour / (2 * MINUTE);
        const int islot_modulo = islot_number % pctx->_txSched._u8_tx_slot_skip;

        static int itx_trigger = 0;
        if(ZERO == islot_modulo)
        {
            if(!itx_trigger)
            {
                itx_trigger = 1;
                if(verbose) StampPrintf("WSPR> Start TX.");

                PioDCOStart(pctx->_pTX->_p_oscillator);
                WSPRbeaconCreatePacket(pctx);
                sleep_ms(100);
                WSPRbeaconSendPacket(pctx);
            }
        }
        else
        {
            itx_trigger = 0;
            if(verbose) StampPrintf("WSPR> Passive TX slot.");
            PioDCOStop(pctx->_pTX->_p_oscillator);
        }
    }

    return 0;
}

/// @brief Dumps the beacon context to stdio.
/// @param pctx Ptr to Context.
void WSPRbeaconDumpContext(const WSPRbeaconContext *pctx)
{
    assert_(pctx);
    assert_(pctx->_pTX);

    const uint64_t u64tmnow = GetUptime64();
    const uint64_t u64_GPS_last_age_sec 
        = (u64tmnow - pctx->_pTX->_p_oscillator->_pGPStime->_time_data._u64_sysclk_nmea_last) / 1000000ULL;

    StampPrintf("__________________");
    StampPrintf("=TxChannelContext=");
    StampPrintf("ftc:%llu", pctx->_pTX->_tm_future_call);
    StampPrintf("ixi:%u", pctx->_pTX->_ix_input);
    StampPrintf("dfq:%lu", pctx->_pTX->_u32_dialfreqhz);
    StampPrintf("gpo:%u", pctx->_pTX->_i_tx_gpio);

    GPStimeContext *pGPS = pctx->_pTX->_p_oscillator->_pGPStime;
    const uint32_t u32_unixtime_now 
            = pctx->_pTX->_p_oscillator->_pGPStime->_time_data._u32_utime_nmea_last + u64_GPS_last_age_sec;
    assert_(pGPS);
    StampPrintf("=GPStimeContext=");
    StampPrintf("err:%ld", pGPS->_i32_error_count);
    StampPrintf("ixw:%lu", pGPS->_u8_ixw);
    StampPrintf("sol:%u", pGPS->_time_data._u8_is_solution_active);
    StampPrintf("unl:%lu", pGPS->_time_data._u32_utime_nmea_last);
    StampPrintf("snl:%llu", pGPS->_time_data._u64_sysclk_nmea_last);
    StampPrintf("age:%llu", u64_GPS_last_age_sec);
    StampPrintf("utm:%lu", u32_unixtime_now);
    StampPrintf("rmc:%lu", pGPS->_time_data._u32_nmea_gprmc_count);
    StampPrintf("pps:%llu", pGPS->_time_data._u64_sysclk_pps_last);
    StampPrintf("ppb:%lld", pGPS->_time_data._i32_freq_shift_ppb);
}

/// @brief Extracts maidenhead type QTH locator (such as KO85) using GPS coords.
/// @param pctx Ptr to WSPR beacon context.
/// @return ptr to string of QTH locator (static duration object inside get_mh).
/// @remark It uses third-party project https://github.com/sp6q/maidenhead .
char *WSPRbeaconGetLastQTHLocator(const WSPRbeaconContext *pctx)
{
    assert_(pctx);
    assert_(pctx->_pTX);
    assert_(pctx->_pTX->_p_oscillator);
    assert_(pctx->_pTX->_p_oscillator->_pGPStime);
    
    const double lat = 1e-5 * (double)pctx->_pTX->_p_oscillator->_pGPStime->_time_data._i64_lat_100k;
    const double lon = 1e-5 * (double)pctx->_pTX->_p_oscillator->_pGPStime->_time_data._i64_lon_100k;

    return get_mh(lat, lon, 8);
}

uint8_t WSPRbeaconIsGPSsolutionActive(const WSPRbeaconContext *pctx)
{
    assert_(pctx);
    assert_(pctx->_pTX);
    assert_(pctx->_pTX->_p_oscillator);
    assert_(pctx->_pTX->_p_oscillator->_pGPStime);

    return YES == pctx->_pTX->_p_oscillator->_pGPStime->_time_data._u8_is_solution_active;
}
