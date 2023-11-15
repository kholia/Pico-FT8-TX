///////////////////////////////////////////////////////////////////////////////
//
//  Roman Piksaykin [piksaykin@gmail.com], R2BDY
//  https://www.qrz.com/db/r2bdy
//
///////////////////////////////////////////////////////////////////////////////
//
//
//  main.c - the Project's entry point.
// 
//
//  DESCRIPTION
//  !FIXME!
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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "defines.h"

#include "pico/multicore.h"
#include "pico-hf-oscillator/lib/assert.h"
#include "pico-hf-oscillator/defines.h"
#include <piodco.h>

#include <WSPRbeacon.h>

#include "debug/logutils.h"

int FSK4mod(uint32_t frq_step_millihz, uint8_t shift_index);

int main()
{
    DEBUGPRINTF("\n");
    sleep_ms(1000);
    DEBUGPRINTF("Pico-WSPR-tx start.");

    DEBUGPRINTF("WSPR beacon init...");
    WSPRbeaconContext *pWB = WSPRbeaconInit("R2BDY", "KO85", 6, FSK4mod);
    DEBUGPRINTF("OK");

    DEBUGPRINTF("Create packet...");
    WSPRbeaconCreatePacket(pWB);
    DEBUGPRINTF("OK");
    
    sleep_ms(100);
    
    int row = 0;
    do
    {
        for(int i = 0; i < 16; ++i)
        {
            const int j = i + row * 16;
            printf("%X ", pWB->_pu8_outbuf[j]);
            if(161 == j)
            {
                row = -1;
                break;
            }
        }
        printf("\n");
        if(-1 == row)
            break;
        ++row;

    } while (true);
    
    for(;;)
    {
        DEBUGPRINTF("tick.");
        sleep_ms(1000);
    }
}

int FSK4mod(uint32_t frq_step_millihz, uint8_t shift_index)
{

    return 0;
}