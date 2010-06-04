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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include "cwiid_internal.h"


/**
 * @brief Enables a Wii Motion Plus (WM+).
 */
int cwiid_enable_motionplus( cwiid_wiimote_t *wiimote )
{
	unsigned char data;
   unsigned char buf[RPT_READ_LEN];
   int ret;
   unsigned char wmid[] = {
      0x00, 0x00, 0xA4, 0x20, 0x04, 0x05 };

   /* Must write 0x04 to 0xA600FE which will generate a status report indicating it's been plugged in. */
   data = 0x04;
   ret = cwiid_write( wiimote, CWIID_RW_REG, 0xA600FE, 1, &data );
   if (ret < 0) {
      /* End wait. */
      /*rpt_wait_end( wiimote, RPT_NULL, NULL, 1 );*/
      return -1;
   }

   /* Race condition \o/. */
   rpt_wait_start( wiimote );
   rpt_wait_end( wiimote, RPT_STATUS, buf, 0 );

   /* Check if the extension was plugged in. */
   if (!(buf[4] & 0x02))
      return -1;

   /* Check to see if plugged in. */
   cwiid_read( wiimote, CWIID_RW_REG, 0xA400FA, 6, &buf );

   /* Check if it's valid. */
   if (memcmp( buf, wmid, 6 ) != 0)
      return -1;
  
   /* Set as detected. */
   wiimote->flags |= CWIID_FLAG_MOTIONPLUS;
   wiimote->state.ext_type = CWIID_EXT_MOTIONPLUS;
   memset( &wiimote->state.ext, 0, sizeof(wiimote->state.ext) );

   return 0;
}


