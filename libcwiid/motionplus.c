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
   int i, ret;

   /* Documentation reports games try up to three times. */
   for (i=0; i<3; i++) {
      /* Try to detect deactivated motionplus. */
      ret = cwiid_read( wiimote, CWIID_RW_REG, 0xA600FA, 6, &buf, 0 );
      if (ret < 0) {
         return 0;
      }

      /* See if it's detected. */
      if (memcmp( buf, wmid, sizeof(wmid) ) != 0) {
         return 0;
      }
      else {
         /* Found! */
         break;
      }

      /* Small delay. */
      usleep( 100 * 1000 );
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
   int extension;
   int i;

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
      rpt_wait_end( wiimote );
      return -1;
   }

   /* Check for status. */
   rpt_wait( wiimote, RPT_STATUS, buf, 0 );

   /* Finished wait. */
   rpt_wait_end( wiimote );

   /* Check if the extension was plugged in.
    * If it wasn't it's most likely because there is another extension hanging on the motionplus. */
   extension = buf[4] & 0x02;

   /* Check to see if plugged in. */
   if (cwiid_read( wiimote, CWIID_RW_REG, 0xA400FA, 6, &buf, 0 ) < 0) {
      cwiid_err( wiimote, "Error powering up MotionPlus" );
      return -1;
   }

   /* Check if it's valid. */
   if (memcmp( buf, wmid, sizeof(wmid) ) != 0) {
      cwiid_err( wiimote, "MotionPlus ID does not match: %02x %02x %02x %02x %02x %02x",
            buf[0], buf[1], buf[2], buf[3], buf[4], buf[5] );
      return -1;
   }

   /* We didn't detect an extension, so try to detect now. */
   if (!extension) {
      /* Start wait. */
      if (rpt_wait_start( wiimote )) {
         return -1;
      }

      /* Try three times. */
      for (i=0; i<3; i++) {

         /* Request status. */
         data = 0x00;
         if (cwiid_send_rpt(wiimote, 0, RPT_STATUS_REQ, 1, &data)) {
            cwiid_err(wiimote, "Status request error");
            rpt_wait_end( wiimote );
            return -1;
         }

         /* Check for status. */
         rpt_wait( wiimote, RPT_STATUS, buf, 1 );

         /* Recheck extension. */
         extension = buf[4] & 0x02;
         if (extension) {
            break;
         }

         /* Small delay. */
         usleep( 100 * 1000 );
      }

      /* Check finally for extension. */
      if (!extension) {
         cwiid_err( wiimote, "Failed to activate MotionPlus (No extension detected)" );
         rpt_wait_end( wiimote );
         return -1;
      }

      /* Finished wait. */
      rpt_wait_end( wiimote );

      /* Try to detect the extension. */
      cwiid_detect_extension( wiimote );
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



