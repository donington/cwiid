/* Copyright (C) 2007 L. Donnie Smith <donnie.smith@gatech.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <unistd.h>

#include "cwiid.h"
#include "cwiid_internal.h"


static const unsigned char nunchuk_id[] =
      { 0x00, 0x00, 0xA4, 0x20, 0x00, 0x00  };
static const unsigned char classic_id[] =
      { 0x00, 0x00, 0xA4, 0x20, 0x01, 0x01  };
static const unsigned char gh3_id[] =
      { 0x00, 0x00, 0xA4, 0x20, 0x01, 0x03  };
static const unsigned char ghdrums_id[] =
      { 0x01, 0x00, 0xA4, 0x20, 0x01, 0x03  };
static const unsigned char balance_id[] =
      { 0x2A, 0x2C };
static const unsigned char motionplus_id[] =
      { 0x00, 0x00, 0xA4, 0x20, 0x04, 0x05 };
static const unsigned char motionplus_nunchuk_id[] =
      { 0x00, 0x00, 0xA4, 0x20, 0x05, 0x05 };
static const unsigned char motionplus_classic_id[] =
      { 0x00, 0x00, 0xA4, 0x20, 0x07, 0x05 };


int cwiid_detect_extension( struct wiimote *wiimote )
{
   unsigned char data[6];
   int ret;

   /* Deactivate encryption. */
   data[0] = 0x55;
   ret = cwiid_write( wiimote, CWIID_RW_REG, 0xA400F0, 1, &data[0] );
   if (ret < 0) {
      cwiid_err( wiimote, "Write error (extension error)" );
      return CWIID_EXT_UNKNOWN;
   }
   data[0] = 0x00;
   ret = cwiid_write( wiimote, CWIID_RW_REG, 0xA400FB, 1, &data[0] );
   if (ret < 0) {
      cwiid_err( wiimote, "Write error (extension error)" );
      return CWIID_EXT_UNKNOWN;
   }

   /* Read ID. */
   ret = cwiid_read( wiimote, CWIID_RW_REG, 0xA400FE, 6, &data );
   if (ret < 0) {
      cwiid_err( wiimote, "Read error (extension error)" );
      return CWIID_EXT_UNKNOWN;
   }

   /* Check now. */
   if (memcmp( data, nunchuk_id, sizeof(nunchuk_id) )==0) {
      return CWIID_EXT_NUNCHUK;
   }
   else if (memcmp( data, classic_id, sizeof(classic_id) )==0) {
      return CWIID_EXT_CLASSIC;
   }
   else if (memcmp( data, balance_id, sizeof(balance_id) )==0) {
      return CWIID_EXT_BALANCE;
   }
   else if ((memcmp( data, motionplus_id, sizeof(motionplus_id) )==0) ||
            (memcmp( data, motionplus_id, sizeof(motionplus_id) )==0) ||
            (memcmp( data, motionplus_id, sizeof(motionplus_id) )==0)) {
      return CWIID_EXT_MOTIONPLUS;
   }

   return CWIID_EXT_NONE;
}


