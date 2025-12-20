///////////////////////////////////////////////////////////////////////////////
//
//  Roman Piksaykin [piksaykin@gmail.com], R2BDY
//  https://www.qrz.com/db/r2bdy
//
///////////////////////////////////////////////////////////////////////////////
//
//
//  flashmem.h - Utility of writing & reading a data from/to Pico's flash mem.
// 
//  DESCRIPTION
//  .
//
//  HOWTOSTART
//  .
//
//  PLATFORM
//      Raspberry Pi pico.
//
//  REVISION HISTORY
// 
//      Rev 0.1   02 Dec 2023
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
#ifndef FLASHMEM_H_
#define FLASHMEM_H_

#include "hardware/flash.h"

typedef struct
{
    void *_pFlashTargetOffset;

} FlashMemContext;

// Initialize flash memory context
void flashmem_init(FlashMemContext *ctx);

// Read WiFi and station configuration from flash
bool flashmem_read_wifi_config(FlashMemContext *ctx, char *ssid, char *password, size_t ssid_size, size_t password_size);
bool flashmem_read_station_config(FlashMemContext *ctx, char *callsign, char *locator, size_t callsign_size, size_t locator_size);
bool flashmem_read_frequency_config(FlashMemContext *ctx, uint32_t *frequency);

// Write WiFi and station configuration to flash
bool flashmem_write_config(FlashMemContext *ctx, const char *ssid, const char *password, const char *callsign, const char *locator, uint32_t frequency);

// Clear WiFi configuration
void flashmem_clear_wifi_config(FlashMemContext *ctx);

#endif
