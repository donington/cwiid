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
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "cwiid_internal.h"


/**
 * @brief Prepares the ground to wait for an RPT message.
 */
int rpt_wait_start( struct wiimote *wiimote )
{
   if (pthread_mutex_lock( &wiimote->router_mutex )) {
      cwiid_err( wiimote, "Mutex lock error (status mutex)" );
      return -1;
   }
   return 0;
}


ssize_t rpt_wait( struct wiimote *wiimote, unsigned char rpt, unsigned char *buf, int process )
{
   /* Make sure not already set. */
   if (wiimote->router_rpt_wait != RPT_NULL) {
      cwiid_err( wiimote, "Some other thread is already waiting on the wiimote rpt queue" );
      return -1;
   }

   /* No RPT_UNLL means to wait. */
   if (rpt != RPT_NULL) {
      /* Set RPT to wait on. */
      wiimote->router_rpt_wait = rpt;
      wiimote->router_rpt_buf  = buf;
      wiimote->router_rpt_process = process;

      /* Wait for conditional to indicate a status reading. */
      if (pthread_cond_wait( &wiimote->router_cond, &wiimote->router_mutex )) {
         cwiid_err( wiimote, "Conditional wait error (status cond)" );
         return -1;
      }
   }

   /* Clear wait RPT. */
   wiimote->router_rpt_wait = RPT_NULL;
   wiimote->router_rpt_buf  = NULL;
   wiimote->router_rpt_process = 1;

   /* Signal thread can continue. */
   if (pthread_cond_broadcast( &wiimote->router_cond )) {
      cwiid_err( wiimote, "Conditional broadcast error (status cond)" );
      return -1;
   }

   /* Success. */
   return 0;
}

/**
 * @brief Actually waits for the RPT message to arrive.
 */
int rpt_wait_end( struct wiimote *wiimote )
{
   if (pthread_mutex_unlock( &wiimote->router_mutex )) {
      cwiid_err( wiimote, "Mutex unlock error (status mutex)");
      return -1;
   }
   return 0;
}


/**
 * @brief Handles routing the packets recieved from the wiimote.
 *
 * Packets can be diverted with rpt_wait_start and rpt_wait_end.
 *
 * @code
 *
 * wiimote --> router thread --> process
 *                  |
 *                  v
 *             rpt_wait_end
 *
 * @endcode
 */
void *router_thread(struct wiimote *wiimote)
{
	unsigned char buf[RPT_READ_LEN];
	ssize_t len;
	struct mesg_array ma;
	char err;

	while (1) {

		/* Read packet */
		len = read( wiimote->int_socket, buf, RPT_READ_LEN );

      /* Print recieved packet. */
#ifdef DEBUG_IO
      int i;
      printf( "IN:  " );
      for (i=0; i<len; i++)
         printf( "%02x ", buf[i] );
      printf( "\n" );
#endif /* DEBUG_IO */

      /* Lock it. */
      if (pthread_mutex_lock( &wiimote->router_mutex )) {
         cwiid_err( wiimote, "Mutex lock error (status mutex)" );
         break;
      }

      /* Initialize the message array. */
      mesg_init( wiimote, &ma );
      /* Already returns error. */

		err = 0;
		if ((len == -1) || (len == 0)) {
			process_error(wiimote, len, &ma);
			write_mesg_array(wiimote, &ma);
			/* Quit! */
         if (pthread_mutex_unlock( &wiimote->router_mutex )) {
            cwiid_err( wiimote, "Mutex unlock error (status mutex)" );
         }
			break;
		}
		else {
			/* Verify first byte (DATA/INPUT) */
			if (buf[0] != (BT_TRANS_DATA | BT_PARAM_INPUT)) {
				cwiid_err(wiimote, "Invalid packet type");
			}

         /* See if we must signal on an RPT. */
         if ((wiimote->router_rpt_wait != RPT_NULL) &&
               (buf[1] == wiimote->router_rpt_wait)) {

            /* Signal recieved. */
            if (pthread_cond_broadcast( &wiimote->router_cond )) {
               cwiid_err( wiimote, "Conditional broadcast error (status cond)" );
               pthread_mutex_unlock( &wiimote->router_mutex );
               break;
            }

            /* Check if we must copy data over. */
            if (wiimote->router_rpt_buf != NULL) {

               /* Copy over. */
               memcpy( wiimote->router_rpt_buf, buf, sizeof(buf) );

               /* See if we must process still. */
               if (!wiimote->router_rpt_process) {
                  /* Unlock. */
                  if (pthread_mutex_unlock( &wiimote->router_mutex )) {
                     cwiid_err( wiimote, "Mutex unlock error (status mutex)" );
                     break;
                  }
                  continue;
               }
            }
         }

			/* Main switch */
			/* printf("%.2X %.2X %.2X %.2X  %.2X %.2X %.2X %.2X\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
			printf("%.2X %.2X %.2X %.2X  %.2X %.2X %.2X %.2X\n", buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
			printf("%.2X %.2X %.2X %.2X  %.2X %.2X %.2X %.2X\n", buf[16], buf[17], buf[18], buf[19], buf[20], buf[21], buf[22], buf[23]);
			printf("\n"); */
			switch (buf[1]) {
			case RPT_STATUS:
				err = process_btn(wiimote, &buf[2], &ma ) ||
                  process_status(wiimote, &buf[4], &ma);
				break;
			case RPT_BTN:
				err = process_btn(wiimote, &buf[2], &ma);
				break;
			case RPT_BTN_ACC:
				err = process_btn(wiimote, &buf[2], &ma) ||
				      process_acc(wiimote, &buf[2], &ma);
				break;
			case RPT_BTN_EXT8:
				err = process_btn(wiimote, &buf[2], &ma) ||
				      process_ext(wiimote, &buf[4], 8, &ma);
				break;
			case RPT_BTN_ACC_IR12:
				err = process_btn(wiimote, &buf[2], &ma) ||
				      process_acc(wiimote, &buf[2], &ma) ||
				      process_ir12(wiimote, &buf[7], &ma);
				break;
			case RPT_BTN_EXT19:
				err = process_btn(wiimote, &buf[2], &ma) ||
				      process_ext(wiimote, &buf[4], 19, &ma);
				break;
			case RPT_BTN_ACC_EXT16:
				err = process_btn(wiimote, &buf[2], &ma) ||
				      process_acc(wiimote, &buf[2], &ma) ||
				      process_ext(wiimote, &buf[7], 16, &ma);
				break;
			case RPT_BTN_IR10_EXT9:
				err = process_btn(wiimote, &buf[2], &ma)  ||
				      process_ir10(wiimote, &buf[4], &ma) ||
				      process_ext(wiimote, &buf[14], 9, &ma);
				break;
			case RPT_BTN_ACC_IR10_EXT6:
				err = process_btn(wiimote, &buf[2], &ma)  ||
				      process_acc(wiimote, &buf[2], &ma)  ||
				      process_ir10(wiimote, &buf[7], &ma) ||
				      process_ext(wiimote, &buf[17], 6, &ma);
				break;
			case RPT_EXT21:
				err = process_ext(wiimote, &buf[2], 21, &ma);
				break;
			case RPT_BTN_ACC_IR36_1:
			case RPT_BTN_ACC_IR36_2:
				cwiid_err(wiimote, "Unsupported report type received "
				                   "(interleaved data)");
				err = 1;
				break;
			case RPT_READ_DATA:
				err = process_btn(wiimote, &buf[2], &ma);
            cwiid_err(wiimote, "Read data event not caught");
            err = 1;
				break;
			case RPT_WRITE_ACK:
            cwiid_err(wiimote, "Write ack event not caught");
            err = 1;
				break;
			default:
				cwiid_err(wiimote, "Unknown message type");
				err = 1;
				break;
			}

         /* Handle messages. */
			if (!err && (ma.count > 0)) {
				if (update_state(wiimote, &ma)) {
					cwiid_err(wiimote, "State update error");
				}
				if (wiimote->flags & CWIID_FLAG_MESG_IFC) {
					/* prints its own errors */
					write_mesg_array(wiimote, &ma);
				}
			}
		}

      /* Unlock. */
      if (pthread_mutex_unlock( &wiimote->router_mutex )) {
         cwiid_err( wiimote, "Mutex unlock error (status mutex)" );
         break;
      }
	}

	return NULL;
}


void *mesg_callback_thread(struct wiimote *wiimote)
{
	int mesg_pipe = wiimote->mesg_pipe[0];
	cwiid_mesg_callback_t *callback = wiimote->mesg_callback;
	struct mesg_array ma;
	int cancelstate;
   int ret;

	while (1) {
		ret = read_mesg_array(mesg_pipe, &ma);
      if (ret != 0) {
			cwiid_err(wiimote, "Mesg pipe read error");
			continue;
		}

		/* TODO: The callback can still be called once after disconnect,
		 * although it's very unlikely.  User must keep track and avoid
		 * accessing the wiimote struct after disconnect. */
		if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancelstate)) {
			cwiid_err(wiimote, "Cancel state disable error (callback thread)");
		}
		callback(wiimote, ma.count, ma.array, &ma.timestamp);
		if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelstate)) {
			cwiid_err(wiimote, "Cancel state restore error (callback thread)");
		}
	}

	return NULL;
}
