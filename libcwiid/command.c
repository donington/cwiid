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

#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include "cwiid_internal.h"

int cwiid_command(cwiid_wiimote_t *wiimote, enum cwiid_command command,
                  int flags) {
	int ret;

	switch (command) {
	case CWIID_CMD_STATUS:
		ret = cwiid_request_status(wiimote);
		break;
	case CWIID_CMD_LED:
		ret = cwiid_set_led(wiimote, flags);
		break;
	case CWIID_CMD_RUMBLE:
		ret = cwiid_set_rumble(wiimote, flags);
		break;
	case CWIID_CMD_RPT_MODE:
		ret = cwiid_set_rpt_mode(wiimote, flags);
		break;
	default:
		ret = -1;
		break;
	}

	return ret;
}

int cwiid_send_rpt(cwiid_wiimote_t *wiimote, uint8_t flags, uint8_t report,
                   size_t len, const void *data)
{
	unsigned char buf[32];
   int ret = 0;

   if (len+2 > sizeof(buf)) {
		cwiid_err( wiimote, "cwiid_send_prt: %d bytes over maximum", len+2-sizeof(buf) );
		return -1;
	}

   /* Lock mutex. */
	if (pthread_mutex_lock(&wiimote->write_mutex)) {
      cwiid_err( wiimote, "Locking mutex (write_mutex)" );
      return -1;
   }

	buf[0] = BT_TRANS_SET_REPORT | BT_PARAM_OUTPUT;
	buf[1] = report;
   if (len > 0) {
      memcpy( &buf[2], data, len );
   }
	if (!(flags & CWIID_SEND_RPT_NO_RUMBLE)) {
		buf[2] |= wiimote->state.rumble;
	}

   /* Debugging. */
#ifdef DEBUG_IO
   int i;
   printf( "OUT: " );
   for (i=0; (size_t)i<len+2; i++)
      printf( "%02x ", buf[i] );
   printf( "\n" );
#endif /* DEBUG_IO */

	if (write(wiimote->ctl_socket, buf, len+2) != (ssize_t)(len+2)) {
		cwiid_err(wiimote, "cwiid_send_rpt: write: %s", strerror(errno));
      ret = -1;
	}
	else if (verify_handshake(wiimote)) {
      ret = -1;
	}

   /* Unlock mutex. */
	if (pthread_mutex_unlock(&wiimote->write_mutex)) {
      cwiid_err( wiimote, "Unlocking mutex (write_mutex)" );
      ret = -1;
   }

	return ret;
}

int cwiid_request_status(cwiid_wiimote_t *wiimote)
{
	unsigned char data;
   int i, ret, retval;

   /* Prepare to wait on event. */
   rpt_wait_start( wiimote );

   retval = -1;
   for (i=0; i<3; i++) {
      /* Send status request. */
      data = 0x00;
      if (cwiid_send_rpt(wiimote, 0, RPT_STATUS_REQ, 1, &data)) {
         cwiid_err(wiimote, "Status request error");
         rpt_wait_end( wiimote );
         return -1;
      }

      /* Wait on event. */
      ret = rpt_wait_timed( wiimote, RPT_STATUS, NULL, 1, 1 );
      if (ret < 0) {
         rpt_wait_end( wiimote );
         return -1;
      }
      /* Got what we wanted. */
      else if (ret == 0) {
         retval = 0;
         break;
      }
   }

   /* Finish wait. */
   rpt_wait_end( wiimote );

	return retval;
}

int cwiid_set_led(cwiid_wiimote_t *wiimote, uint8_t led)
{
	unsigned char data;

	/* TODO: assumption: char assignments are atomic, no mutex lock needed */
	wiimote->state.led = led & 0x0F;
	data = wiimote->state.led << 4;
	if (cwiid_send_rpt(wiimote, 0, RPT_LED_RUMBLE, 1, &data)) {
		cwiid_err(wiimote, "Report send error (led)");
		return -1;
	}

	return 0;
}

int cwiid_set_rumble(cwiid_wiimote_t *wiimote, uint8_t rumble)
{
	unsigned char data;

	/* TODO: assumption: char assignments are atomic, no mutex lock needed */
	wiimote->state.rumble = rumble ? 1 : 0;
	data = wiimote->state.led << 4;
	if (cwiid_send_rpt(wiimote, 0, RPT_LED_RUMBLE, 1, &data)) {
		cwiid_err(wiimote, "Report send error (led)");
		return -1;
	}

	return 0;
}

int cwiid_set_rpt_mode(cwiid_wiimote_t *wiimote, uint8_t rpt_mode)
{
	return update_rpt_mode(wiimote, rpt_mode);
}

int cwiid_read(cwiid_wiimote_t *wiimote, uint8_t flags, uint32_t offset,
               uint16_t len, void *data, int waitting)
{
	unsigned char buf[RPT_READ_LEN];
   int err, slen;

	/* Compose read request packet */
	buf[0] = flags & (CWIID_RW_EEPROM | CWIID_RW_REG);
	buf[1] = (unsigned char)((offset>>16) & 0xFF);
	buf[2] = (unsigned char)((offset>>8) & 0xFF);
	buf[3] = (unsigned char)(offset & 0xFF);
	buf[4] = (unsigned char)((len>>8) & 0xFF);
	buf[5] = (unsigned char)(len & 0xFF);

   /* Start wait. */
   if ((!waitting) && rpt_wait_start( wiimote )) {
      return -1;
   }

	/* TODO: Document: user is responsible for ensuring that read/write
	 * operations are not in flight while disconnecting.  Nothing serious,
	 * just accesses to freed memory */
	/* Send read request packet */
	if (cwiid_send_rpt(wiimote, 0, RPT_READ_REQ, 6, buf)) {
		cwiid_err(wiimote, "Report send error (read)");
      if (!waitting) {
         rpt_wait_end( wiimote );
      }
      return -1;
	}

   /* Get the packet. */
   rpt_wait( wiimote, RPT_READ_DATA, buf, 0 );

   /* Process. */
   err      = buf[4] & 0x0F;
   slen     = (buf[4]>>4)+1;
   memcpy( data, &buf[7], slen );

   /* Finish wait. */
   if (!waitting) {
      rpt_wait_end( wiimote );
   }

   if (err) {
      return -err;
   }
   return slen;
}

int cwiid_write(cwiid_wiimote_t *wiimote, uint8_t flags, uint32_t offset,
                  uint16_t len, const void *data, int waitting)
{
	unsigned char buf[RPT_READ_LEN];
	uint16_t sent=0;
   unsigned char err;

	/* Compose write packet header */
	buf[0] = flags;

   if ((!waitting) && rpt_wait_start( wiimote )) {
      return -1;
   }

	/* Send packets */
   err = 0;
	while ((!err) && (sent < len)) {
		/* Compose write packet */
		buf[1] = (unsigned char)(((offset+sent)>>16) & 0xFF);
		buf[2] = (unsigned char)(((offset+sent)>>8) & 0xFF);
		buf[3] = (unsigned char)((offset+sent) & 0xFF);
		if (len-sent >= 0x10) {
			buf[4]=(unsigned char)0x10;
		}
		else {
			buf[4]=(unsigned char)(len-sent);
		}
		memcpy( buf+5, data+sent, buf[4] );

		if (cwiid_send_rpt(wiimote, 0, RPT_WRITE, 21, buf)) {
			cwiid_err(wiimote, "Report send error (write)");
         if (!waitting) {
            rpt_wait_end( wiimote );
         }
         return -1;
		}

      /* Wait on event. */
      rpt_wait( wiimote, RPT_WRITE_ACK, buf, 0 );

      /* Check if it reports an error. */
      err = buf[5];

      if (!err) {
         sent += buf[4];
      }
	}

   if (!waitting) {
      rpt_wait_end( wiimote );
   }

   if (err) {
      return -err;
   }
   return sent;
}


struct write_seq speaker_enable_seq[] = {
	{WRITE_SEQ_RPT, RPT_SPEAKER_ENABLE, (const void *)"\x04", 1, 0},
	{WRITE_SEQ_RPT,   RPT_SPEAKER_MUTE, (const void *)"\x04", 1, 0},
	{WRITE_SEQ_MEM, 0xA20009, (const void *)"\x01", 1, CWIID_RW_REG},
	{WRITE_SEQ_MEM, 0xA20001, (const void *)"\x08", 1, CWIID_RW_REG},
	{WRITE_SEQ_MEM, 0xA20001, (const void *)"\x00\x00\x00\x0C\x40\x00\x00",
	                          7, CWIID_RW_REG},
	{WRITE_SEQ_MEM, 0xA20008, (const void *)"\x01", 1, CWIID_RW_REG},
	{WRITE_SEQ_RPT,   RPT_SPEAKER_MUTE, (const void *)"\x00", 1, 0}
};

struct write_seq speaker_disable_seq[] = {
	{WRITE_SEQ_RPT,   RPT_SPEAKER_MUTE, (const void *)"\x04", 1, 0},
	{WRITE_SEQ_RPT, RPT_SPEAKER_ENABLE, (const void *)"\x00", 1, 0}
};

#define SOUND_BUF_LEN	21
int cwiid_beep(cwiid_wiimote_t *wiimote)
{
	/* unsigned char buf[SOUND_BUF_LEN] = { 0xA0, 0xCC, 0x33, 0xCC, 0x33,
	    0xCC, 0x33, 0xCC, 0x33, 0xCC, 0x33, 0xCC, 0x33, 0xCC, 0x33, 0xCC, 0x33,
	    0xCC, 0x33, 0xCC, 0x33}; */
	unsigned char buf[SOUND_BUF_LEN] = { 0xA0, 0xC3, 0xC3, 0xC3, 0xC3,
	    0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3,
	    0xC3, 0xC3, 0xC3, 0xC3};
	int i;
	int ret = 0;
	pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t timer_cond = PTHREAD_COND_INITIALIZER;
	struct timespec t;

	if (exec_write_seq(wiimote, SEQ_LEN(speaker_enable_seq),
	                   speaker_enable_seq)) {
		cwiid_err(wiimote, "Speaker enable error");
		ret = -1;
	}

	pthread_mutex_lock(&timer_mutex);

	for (i=0; i<100; i++) {
		clock_gettime(CLOCK_REALTIME, &t);
		t.tv_nsec += 10204081;
		/* t.tv_nsec += 7000000; */
		if (cwiid_send_rpt(wiimote, 0, RPT_SPEAKER_DATA, SOUND_BUF_LEN, buf)) {
		 	printf("%d\n", i);
			cwiid_err(wiimote, "Report send error (speaker data)");
			ret = -1;
			break;
		}
		/* TODO: I should be shot for this, but hey, it works.
		 * longterm - find a better wait */
		pthread_cond_timedwait(&timer_cond, &timer_mutex, &t);
	}

	pthread_mutex_unlock(&timer_mutex);

	if (exec_write_seq(wiimote, SEQ_LEN(speaker_disable_seq),
	                   speaker_disable_seq)) {
		cwiid_err(wiimote, "Speaker disable error");
		ret = -1;
	}

	return ret;
}
