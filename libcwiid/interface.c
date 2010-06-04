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

int cwiid_get_id(cwiid_wiimote_t *wiimote)
{
	return wiimote->id;
}

int cwiid_set_data(cwiid_wiimote_t *wiimote, const void *data)
{
	wiimote->data = data;
	return 0;
}

const void *cwiid_get_data(cwiid_wiimote_t *wiimote)
{
	return wiimote->data;
}


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

int cwiid_enable(cwiid_wiimote_t *wiimote, int flags)
{
   /* Set flags. */
   wiimote->flags |= (flags & ~CWIID_FLAG_MOTIONPLUS);

	if ((flags & CWIID_FLAG_NONBLOCK) &&
	  !(wiimote->flags & CWIID_FLAG_NONBLOCK)) {
		if (fcntl(wiimote->mesg_pipe[0], F_SETFL, O_NONBLOCK)) {
			cwiid_err(wiimote, "File control error (mesg pipe)");
			return -1;
		}
	}
	if (flags & CWIID_FLAG_MOTIONPLUS) {
      cwiid_enable_motionplus( wiimote );
	}
	return 0;
}

int cwiid_disable(cwiid_wiimote_t *wiimote, int flags)
{
	unsigned char data;

	if ((flags & CWIID_FLAG_NONBLOCK) &&
	  (wiimote->flags & CWIID_FLAG_NONBLOCK)) {
		if (fcntl(wiimote->mesg_pipe[0], F_SETFL, 0)) {
			cwiid_err(wiimote, "File control error (mesg pipe)");
			return -1;
		}
	}
	if (flags & CWIID_FLAG_MOTIONPLUS) {
		data = 0x55;
		cwiid_write(wiimote, CWIID_RW_REG, 0xA400F0, 1, &data);
		data = 0x00;
		cwiid_write(wiimote, CWIID_RW_REG, 0xA400FB, 1, &data);
		cwiid_request_status(wiimote);
	}
	wiimote->flags &= ~flags;
	return 0;
}

int cwiid_set_mesg_callback(cwiid_wiimote_t *wiimote,
                            cwiid_mesg_callback_t *callback)
{
	if (wiimote->mesg_callback) {
		if (cancel_mesg_callback(wiimote)) {
			/* prints it's own errors */
			return -1;
		}
	}

	wiimote->mesg_callback = callback;

	if (wiimote->mesg_callback) {
		if (pthread_create(&wiimote->mesg_callback_thread, NULL,
		                  (void *(*)(void *))&mesg_callback_thread, wiimote)) {
			cwiid_err(wiimote, "Thread creation error (callback thread)");
			return -1;
		}
	}

	return 0;
}

int cwiid_get_mesg(cwiid_wiimote_t *wiimote, int *mesg_count,
                   union cwiid_mesg *mesg[], struct timespec *timestamp)
{
	struct mesg_array ma;

	if (read_mesg_array(wiimote->mesg_pipe[0], &ma)) {
		if (errno == EAGAIN) {
			return -1;
		}
		else {
			cwiid_err(wiimote, "Pipe read error (mesg_pipe)");
			return -1;
		}
	}

	*mesg_count = ma.count;
	*timestamp = ma.timestamp;

	if ((*mesg = malloc(ma.count * sizeof ma.array[0])) == NULL) {
		cwiid_err(wiimote, "Memory allocation error (mesg array)");
		return -1;
	}

	memcpy(*mesg, &ma.array, ma.count * sizeof (*mesg)[0]);

	return 0;
}

int cwiid_get_state(cwiid_wiimote_t *wiimote, struct cwiid_state *state)
{
	if (pthread_mutex_lock(&wiimote->state_mutex)) {
		cwiid_err(wiimote, "Mutex lock error (state mutex)");
		return -1;
	}

	memcpy(state, &wiimote->state, sizeof *state);

	if (pthread_mutex_unlock(&wiimote->state_mutex)) {
		cwiid_err(wiimote, "Mutex unlock error (state mutex) - "
		                   "deadlock warning");
		return -1;
	}

	return 0;
}

int cwiid_get_acc_cal(cwiid_wiimote_t *wiimote, enum cwiid_ext_type ext_type,
                      struct acc_cal *acc_cal)
{
	uint8_t flags;
	uint32_t offset;
	unsigned char buf[7];
	char *err_str;

	switch (ext_type) {
	case CWIID_EXT_NONE:
		flags = CWIID_RW_EEPROM;
		offset = 0x16;
		err_str = "";
		break;
	case CWIID_EXT_NUNCHUK:
		flags = CWIID_RW_REG;
		offset = 0xA40020;
		err_str = "nunchuk ";
		break;
	default:
		cwiid_err(wiimote, "Unsupported calibration request");
		return -1;
	}
	if (cwiid_read(wiimote, flags, offset, 7, buf)) {
		cwiid_err(wiimote, "Read error (%scal)", err_str);
		return -1;
	}

	acc_cal->zero[CWIID_X] = buf[0];
	acc_cal->zero[CWIID_Y] = buf[1];
	acc_cal->zero[CWIID_Z] = buf[2];
	acc_cal->one[CWIID_X]  = buf[4];
	acc_cal->one[CWIID_Y]  = buf[5];
	acc_cal->one[CWIID_Z]  = buf[6];

	return 0;
}

int cwiid_get_balance_cal(cwiid_wiimote_t *wiimote,
                          struct balance_cal *balance_cal)
{
	unsigned char buf[24];

	if (cwiid_read(wiimote, CWIID_RW_REG, 0xa40024, 24, buf)) {
		cwiid_err(wiimote, "Read error (balancecal)");
		return -1;
	}
	balance_cal->right_top[0]    = ((uint16_t)buf[0]<<8 | (uint16_t)buf[1]);
	balance_cal->right_bottom[0] = ((uint16_t)buf[2]<<8 | (uint16_t)buf[3]);
	balance_cal->left_top[0]     = ((uint16_t)buf[4]<<8 | (uint16_t)buf[5]);
	balance_cal->left_bottom[0]  = ((uint16_t)buf[6]<<8 | (uint16_t)buf[7]);
	balance_cal->right_top[1]    = ((uint16_t)buf[8]<<8 | (uint16_t)buf[9]);
	balance_cal->right_bottom[1] = ((uint16_t)buf[10]<<8 | (uint16_t)buf[11]);
	balance_cal->left_top[1]     = ((uint16_t)buf[12]<<8 | (uint16_t)buf[13]);
	balance_cal->left_bottom[1]  = ((uint16_t)buf[14]<<8 | (uint16_t)buf[15]);
	balance_cal->right_top[2]    = ((uint16_t)buf[16]<<8 | (uint16_t)buf[17]);
	balance_cal->right_bottom[2] = ((uint16_t)buf[18]<<8 | (uint16_t)buf[19]);
	balance_cal->left_top[2]     = ((uint16_t)buf[20]<<8 | (uint16_t)buf[21]);
	balance_cal->left_bottom[2]  = ((uint16_t)buf[22]<<8 | (uint16_t)buf[23]);

	return 0;
}
