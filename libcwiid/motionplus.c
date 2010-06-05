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
 * @brief Tries to detect a disabled motionplus.
 */
int cwiid_detect_motionplus( cwiid_wiimote_t *wiimote )
{
   unsigned char wmid[] = {
         0x00, 0x00, 0xA6, 0x20, 0x00, 0x05 };
   unsigned char buf[RPT_READ_LEN];
   int i;

   /* Documentation reports games try up to three times. */
   for (i=0; i<3; i++) {
      /* Try to detect deactivated motionplus. */
      cwiid_read( wiimote, CWIID_RW_REG, 0xA600FA, 6, &buf, 0 );

      /* See if it's detected. */
      if (memcmp( buf, wmid, 6 ) != 0)
         return 0;
   }

   return 1;
}


/**
 * @brief Enables a Wii Motion Plus (WM+).
 *
 *    @param wiimote Wiimote to enable motion plus on.
 */
int cwiid_enable_motionplus( cwiid_wiimote_t *wiimote )
{
	unsigned char data;
   unsigned char buf[RPT_READ_LEN];
   int ret;
   unsigned char wmid[] = {
         0x00, 0x00, 0xA4, 0x20, 0x04, 0x05 };

   /* Sanity check to see if it's already connected. */
   if (wiimote->state.ext_type == CWIID_EXT_MOTIONPLUS) {
      return 0;
   }

   /* Try to detect it. */
   if (!cwiid_detect_motionplus( wiimote )) {
      return -1;
   }

   /* Start wait. */
   if (rpt_wait_start( wiimote )) {
      return -1;
   }

   /* Must write 0x04 to 0xA600FE which will generate a status report indicating it's been plugged in. */
   data = 0x04;
   ret = cwiid_write( wiimote, CWIID_RW_REG, 0xA600FE, 1, &data, 1 );
   if (ret < 0) {
      /* End wait. */
      /*rpt_wait_end( wiimote, RPT_NULL, NULL, 1 );*/
      return -1;
   }

   /* Check for status. */
   rpt_wait( wiimote, RPT_STATUS, buf, 0 );

   /* Finished wait. */
   rpt_wait_end( wiimote );

   /* Check if the extension was plugged in. */
   if (!(buf[4] & 0x02)) {
      return -1;
   }

   /* Check to see if plugged in. */
   if (cwiid_read( wiimote, CWIID_RW_REG, 0xA400FA, 6, &buf, 0 )) {
      return -1;
   }

   /* Check if it's valid. */
   if (memcmp( buf, wmid, 6 ) != 0) {
      return -1;
   }
  
   /* Set as detected. */
   wiimote->flags |= CWIID_FLAG_MOTIONPLUS;
   wiimote->state.ext_type = CWIID_EXT_MOTIONPLUS;
   memset( &wiimote->state.ext, 0, sizeof(wiimote->state.ext) );

   return 0;
}


/**
 * @brief Disables a connected motionplus.
 *
 *    @param wiimote Wiimote to disable motionplus on.
 */
int cwiid_disable_motionplus( cwiid_wiimote_t *wiimote )
{
   unsigned char data;
   int ret;

   /* Sanity check to see if it's already connected. */
   if (wiimote->state.ext_type != CWIID_EXT_MOTIONPLUS)
      return 0;

   /* Disable it. */
   data = 0x55;
   ret = cwiid_write( wiimote, CWIID_RW_REG, 0xA400F0, 1, &data, 0 );
   if (ret < 0) {
      return -1;
   }

   /* Initialize other extensions. */
   data = 0x00;
   ret = cwiid_write( wiimote, CWIID_RW_REG, 0x4A400FB, 1, &data, 0 );
   if (ret < 0) {
      return -1;
   }

   /* Disable extension. */
   wiimote->state.ext_type = CWIID_EXT_NONE;
   wiimote->flags &= ~CWIID_FLAG_MOTIONPLUS;
 
   return 0;
}



