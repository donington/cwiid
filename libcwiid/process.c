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
#include <string.h>
#include <unistd.h>
#include "cwiid_internal.h"

int process_error(struct wiimote *wiimote, ssize_t len, struct mesg_array *ma)
{
	struct cwiid_error_mesg *error_mesg;

	error_mesg = &ma->array[ma->count++].error_mesg;
	error_mesg->type = CWIID_MESG_ERROR;
	if (len == 0) {
		error_mesg->error = CWIID_ERROR_DISCONNECT;
	}
	else {
		error_mesg->error = CWIID_ERROR_COMM;
	}

	if (cancel_rw(wiimote)) {
		cwiid_err(wiimote, "RW cancel error");
	}

	return 0;
}

int process_status(struct wiimote *wiimote, const unsigned char *data,
                   struct mesg_array *ma)
{
	struct cwiid_status_mesg status_mesg;

	(void)ma;

	status_mesg.type = CWIID_MESG_STATUS;
	status_mesg.battery = data[5];
	if (data[2] & 0x02) {
		/* status_thread will figure out what it is */
		status_mesg.ext_type = CWIID_EXT_UNKNOWN;
	}
	else {
		status_mesg.ext_type = CWIID_EXT_NONE;
	}

	if (write(wiimote->status_pipe[1], &status_mesg, sizeof status_mesg)
	  != sizeof status_mesg) {
		cwiid_err(wiimote, "Status pipe write error: %s", strerror(errno));
		return -1;
	}

	return 0;
}

int process_btn(struct wiimote *wiimote, const unsigned char *data,
                struct mesg_array *ma)
{
	struct cwiid_btn_mesg *btn_mesg;
	uint16_t buttons;

	buttons = (data[0] & BTN_MASK_0)<<8 |
	          (data[1] & BTN_MASK_1);
	if (wiimote->state.rpt_mode & CWIID_RPT_BTN) {
		if ((wiimote->state.buttons != buttons) ||
		  (wiimote->flags & CWIID_FLAG_REPEAT_BTN)) {
			btn_mesg = &ma->array[ma->count++].btn_mesg;
			btn_mesg->type = CWIID_MESG_BTN;
			btn_mesg->buttons = buttons;
		}
	}

	return 0;
}

int process_acc(struct wiimote *wiimote, const unsigned char *data,
                struct mesg_array *ma)
{
	struct cwiid_acc_mesg *acc_mesg;

	if (wiimote->state.rpt_mode & CWIID_RPT_ACC) {
		acc_mesg = &ma->array[ma->count++].acc_mesg;
		acc_mesg->type = CWIID_MESG_ACC;
		acc_mesg->acc[CWIID_X] = ((uint16_t)data[2] << 2) |
                  (((uint16_t)data[0] & (3<<5)) >> 5);
		acc_mesg->acc[CWIID_Y] = ((uint16_t)data[3] << 2) |
                  (((uint16_t)data[1] & (1<<5)) >> 4);
		acc_mesg->acc[CWIID_Z] = ((uint16_t)data[4] << 2) |
                   (((uint16_t)data[1] & (1<<6)) >> 5);
	}

	return 0;
}

int process_ir10(struct wiimote *wiimote, const unsigned char *data,
                 struct mesg_array *ma)
{
	struct cwiid_ir_mesg *ir_mesg;
	int i;
	const unsigned char *block;

	if (wiimote->state.rpt_mode & CWIID_RPT_IR) {
		ir_mesg = &ma->array[ma->count++].ir_mesg;
		ir_mesg->type = CWIID_MESG_IR;

		for (i=0, block=data; i < CWIID_IR_SRC_COUNT; i+=2, block+=5) {
			if (block[0] == 0xFF) {
				ir_mesg->src[i].valid = 0;
			}
			else {
				ir_mesg->src[i].valid = 1;
				ir_mesg->src[i].pos[CWIID_X] = ((uint16_t)block[2] & 0x30)<<4 |
				                                (uint16_t)block[0];
				ir_mesg->src[i].pos[CWIID_Y] = ((uint16_t)block[2] & 0xC0)<<2 |
				                                (uint16_t)block[1];
				ir_mesg->src[i].size = -1;
			}

			if (block[3] == 0xFF) {
				ir_mesg->src[i+1].valid = 0;
			}
			else {
				ir_mesg->src[i+1].valid = 1;
				ir_mesg->src[i+1].pos[CWIID_X] =
				                               ((uint16_t)block[2] & 0x03)<<8 |
				                                (uint16_t)block[3];
				ir_mesg->src[i+1].pos[CWIID_Y] =
				                               ((uint16_t)block[2] & 0x0C)<<6 |
				                                (uint16_t)block[4];
				ir_mesg->src[i+1].size = -1;
			}
		}
	}

	return 0;
}

int process_ir12(struct wiimote *wiimote, const unsigned char *data,
                 struct mesg_array *ma)
{
	struct cwiid_ir_mesg *ir_mesg;
	int i;
	const unsigned char *block;

	if (wiimote->state.rpt_mode & CWIID_RPT_IR) {
		ir_mesg = &ma->array[ma->count++].ir_mesg;
		ir_mesg->type = CWIID_MESG_IR;

		for (i=0, block=data; i < CWIID_IR_SRC_COUNT; i++, block+=3) {
			if (block[0] == 0xFF) {
				ir_mesg->src[i].valid = 0;
			}
			else {
				ir_mesg->src[i].valid = 1;
				ir_mesg->src[i].pos[CWIID_X] = ((uint16_t)block[2] & 0x30)<<4 |
				                                (uint16_t)block[0];
				ir_mesg->src[i].pos[CWIID_Y] = ((uint16_t)block[2] & 0xC0)<<2 |
				                                (uint16_t)block[1];
				ir_mesg->src[i].size = block[2] & 0x0F;
			}
		}
	}

	return 0;
}

int process_ext(struct wiimote *wiimote, unsigned char *data,
                unsigned char len, struct mesg_array *ma)
{
	struct cwiid_nunchuk_mesg *nunchuk_mesg;
	struct cwiid_classic_mesg *classic_mesg;
	struct cwiid_balance_mesg *balance_mesg;
	struct cwiid_motionplus_mesg *motionplus_mesg;
	struct cwiid_guitar_mesg *guitar_mesg;
	struct cwiid_drums_mesg *drums_mesg;
	struct cwiid_turntables_mesg *turntables_mesg;

	int i;

	(void)len;

	switch (wiimote->state.ext_type) {
	case CWIID_EXT_NONE:
		cwiid_err(wiimote, "Received unexpected extension report");
		break;
	case CWIID_EXT_UNKNOWN:
		break;
	case CWIID_EXT_NUNCHUK:
		if (wiimote->state.rpt_mode & CWIID_RPT_NUNCHUK) {
			nunchuk_mesg = &ma->array[ma->count++].nunchuk_mesg;
			nunchuk_mesg->type = CWIID_MESG_NUNCHUK;
			nunchuk_mesg->stick[CWIID_X] = data[0];
			nunchuk_mesg->stick[CWIID_Y] = data[1];
			nunchuk_mesg->acc[CWIID_X]   = ((uint16_t)data[2]<<2) |
                          (((uint16_t)data[5] & (3 << 2)) >> 2);
			nunchuk_mesg->acc[CWIID_Y]   = ((uint16_t)data[3]<<2) |
                          (((uint16_t)data[5] & (3 << 4)) >> 4);
			nunchuk_mesg->acc[CWIID_Z]   = ((uint16_t)data[4]<<2) |
                          (((uint16_t)data[5] & (3 << 6)) >> 6);
			nunchuk_mesg->buttons = ~data[5] & NUNCHUK_BTN_MASK;
		}
		break;
	case CWIID_EXT_CLASSIC:
		if (wiimote->state.rpt_mode & CWIID_RPT_CLASSIC) {
			classic_mesg = &ma->array[ma->count++].classic_mesg;
			classic_mesg->type = CWIID_MESG_CLASSIC;

			for (i=0; i < 6; i++) {
				data[i] = data[i];
			}

			classic_mesg->l_stick[CWIID_X] = data[0] & 0x3F;
			classic_mesg->l_stick[CWIID_Y] = data[1] & 0x3F;
			classic_mesg->r_stick[CWIID_X] = (data[0] & 0xC0)>>3 |
			                                 (data[1] & 0xC0)>>5 |
			                                 (data[2] & 0x80)>>7;
			classic_mesg->r_stick[CWIID_Y] = data[2] & 0x1F;
			classic_mesg->l = (data[2] & 0x60)>>2 |
			                  (data[3] & 0xE0)>>5;
			classic_mesg->r = data[3] & 0x1F;
			classic_mesg->buttons = ~((uint16_t)data[4]<<8 |
			                          (uint16_t)data[5]);
		}
		break;
	case CWIID_EXT_BALANCE:
		if (wiimote->state.rpt_mode & CWIID_RPT_BALANCE) {
			balance_mesg = &ma->array[ma->count++].balance_mesg;
			balance_mesg->type = CWIID_MESG_BALANCE;
			balance_mesg->right_top = ((uint16_t)data[0]<<8 |
			                           (uint16_t)data[1]);
			balance_mesg->right_bottom = ((uint16_t)data[2]<<8 |
			                              (uint16_t)data[3]);
			balance_mesg->left_top = ((uint16_t)data[4]<<8 |
			                          (uint16_t)data[5]);
			balance_mesg->left_bottom = ((uint16_t)data[6]<<8 |
			                             (uint16_t)data[7]);
		}
		break;
	case CWIID_EXT_MOTIONPLUS:
		/* motionplus data. */
		if (((uint8_t)data[5] & 0x02) == 0x02) {
		  if (wiimote->state.rpt_mode & CWIID_RPT_MOTIONPLUS) {
			motionplus_mesg = &ma->array[ma->count++].motionplus_mesg;
			motionplus_mesg->type = CWIID_MESG_MOTIONPLUS;
			motionplus_mesg->angle_rate[CWIID_PHI]   = ((uint16_t)data[5] & 0xFC)<<6 | (uint16_t)data[2];
			motionplus_mesg->angle_rate[CWIID_THETA] = ((uint16_t)data[4] & 0xFC)<<6 | (uint16_t)data[1];
			motionplus_mesg->angle_rate[CWIID_PSI]   = ((uint16_t)data[3] & 0xFC)<<6 | (uint16_t)data[0];
			motionplus_mesg->low_speed[CWIID_PHI]    = ((uint8_t)data[3] & 0x01);
			motionplus_mesg->low_speed[CWIID_THETA]  = ((uint8_t)data[4] & 0x02)>>1;
			motionplus_mesg->low_speed[CWIID_PSI]    = ((uint8_t)data[3] & 0x02)>>1;
			motionplus_mesg->extension               = ((uint8_t)data[4] & 0x01);
		  }
		}
		/* nunchuk passthrough data. */
		else if (((uint8_t)data[5] & 0x02) == 0x00) {
		//else {
		  if (wiimote->state.rpt_mode & CWIID_RPT_NUNCHUK) {
			nunchuk_mesg = &ma->array[ma->count++].nunchuk_mesg;
			nunchuk_mesg->type = CWIID_MESG_NUNCHUK;
			nunchuk_mesg->stick[CWIID_X] = data[0];
			nunchuk_mesg->stick[CWIID_Y] = data[1];
			nunchuk_mesg->acc[CWIID_X]   = ((uint16_t)data[2]<<2) | (((uint16_t)data[5] & (1<<4)) >> 3);
			nunchuk_mesg->acc[CWIID_Y]   = ((uint16_t)data[3]<<2) | (((uint16_t)data[5] & (1<<5)) >> 4);
			nunchuk_mesg->acc[CWIID_Z]   = ((uint16_t)(data[4] & ~1)<<2) | ((uint16_t)data[5] & (3<<6)) >> 5;
			nunchuk_mesg->buttons = ~((data[5] & (1<<3 | 1<<2)) >> 2);
		  }
		}
		break;
	case CWIID_EXT_GUITAR:
		if (wiimote->state.rpt_mode & CWIID_RPT_GUITAR) {
			guitar_mesg = &ma->array[ma->count++].guitar_mesg;
			guitar_mesg->type = CWIID_MESG_GUITAR;
			guitar_mesg->stick[CWIID_X] = data[0] & CWIID_GUITAR_STICK_MAX;
			guitar_mesg->stick[CWIID_Y] = data[1] & CWIID_GUITAR_STICK_MAX;
			guitar_mesg->whammy = data[3] & CWIID_GUITAR_WHAMMY_MAX;
			guitar_mesg->buttons = ~((uint16_t)data[4]<<8 |
			                         (uint16_t)data[5]);
			unsigned int touch_bar_data = data[2] & CWIID_GUITAR_TOUCH_BAR_MAX;
			if (touch_bar_data == CWIID_GUITAR_TOUCHBAR_VALUE_NONE) {
				guitar_mesg->touch_bar = CWIID_GUITAR_TOUCHBAR_NONE;
			} else if (touch_bar_data < CWIID_GUITAR_TOUCHBAR_VALUE_1ST_AND_2ND) {
				guitar_mesg->touch_bar = CWIID_GUITAR_TOUCHBAR_1ST;
			} else if (touch_bar_data < CWIID_GUITAR_TOUCHBAR_VALUE_2ND) {
				guitar_mesg->touch_bar = CWIID_GUITAR_TOUCHBAR_1ST_AND_2ND;
			} else if (touch_bar_data < CWIID_GUITAR_TOUCHBAR_VALUE_2ND_AND_3RD) {
				guitar_mesg->touch_bar = CWIID_GUITAR_TOUCHBAR_2ND;
			} else if (touch_bar_data < CWIID_GUITAR_TOUCHBAR_VALUE_3RD) {
				guitar_mesg->touch_bar = CWIID_GUITAR_TOUCHBAR_2ND_AND_3RD;
			} else if (touch_bar_data < CWIID_GUITAR_TOUCHBAR_VALUE_3RD_AND_4TH) {
				guitar_mesg->touch_bar = CWIID_GUITAR_TOUCHBAR_3RD;
			} else if (touch_bar_data < CWIID_GUITAR_TOUCHBAR_VALUE_4TH) {
				guitar_mesg->touch_bar = CWIID_GUITAR_TOUCHBAR_3RD_AND_4TH;
			} else if (touch_bar_data < CWIID_GUITAR_TOUCHBAR_VALUE_4TH_AND_5TH) {
				guitar_mesg->touch_bar = CWIID_GUITAR_TOUCHBAR_4TH;
			} else if (touch_bar_data < CWIID_GUITAR_TOUCHBAR_VALUE_5TH) {
				guitar_mesg->touch_bar = CWIID_GUITAR_TOUCHBAR_4TH_AND_5TH;
			} else {
				guitar_mesg->touch_bar = CWIID_GUITAR_TOUCHBAR_5TH;
			}

		}
		break;
	case CWIID_EXT_DRUMS:
		if (wiimote->state.rpt_mode & CWIID_RPT_DRUMS) {
/*		
		#define BYTETOBINARYPATTERN "%d%d%d%d%d%d%d%d"
#define BYTETOBINARY(byte)  \
  (byte & 0x80 ? 1 : 0), \
  (byte & 0x40 ? 1 : 0), \
  (byte & 0x20 ? 1 : 0), \
  (byte & 0x10 ? 1 : 0), \
  (byte & 0x08 ? 1 : 0), \
  (byte & 0x04 ? 1 : 0), \
  (byte & 0x02 ? 1 : 0), \
  (byte & 0x01 ? 1 : 0) 
  printf("data:\n"BYTETOBINARYPATTERN"\n"BYTETOBINARYPATTERN"\n"BYTETOBINARYPATTERN"\n", BYTETOBINARY(data[0]), BYTETOBINARY(data[1]), BYTETOBINARY(data[2]));
  printf(BYTETOBINARYPATTERN"\n"BYTETOBINARYPATTERN"\n"BYTETOBINARYPATTERN"\n", BYTETOBINARY(data[3]), BYTETOBINARY(data[4]), BYTETOBINARY(data[5]));
*/  
			drums_mesg = &ma->array[ma->count++].drums_mesg;
			drums_mesg->type = CWIID_MESG_DRUMS;
			drums_mesg->stick[CWIID_X] = data[0] & CWIID_DRUMS_STICK_MAX;
			drums_mesg->stick[CWIID_Y] = data[1] & CWIID_DRUMS_STICK_MAX;

			if ((uint8_t)data[2] & 0x40) {
				switch (((uint8_t)data[2] & 0x3E) >> 1) {
					case 0x0E:
						drums_mesg->velocity_source = CWIID_DRUMS_VELOCITY_SOURCE_ORANGE;
						break;
					case 0x0F:
						drums_mesg->velocity_source = CWIID_DRUMS_VELOCITY_SOURCE_BLUE;
						break;
					case 0x11:
						drums_mesg->velocity_source = CWIID_DRUMS_VELOCITY_SOURCE_YELLOW;
						break;
					case 0x12:
						drums_mesg->velocity_source = CWIID_DRUMS_VELOCITY_SOURCE_GREEN;
						break;
					case 0x19:
						drums_mesg->velocity_source = CWIID_DRUMS_VELOCITY_SOURCE_RED;
						break;
					case 0x1B:
						drums_mesg->velocity_source = CWIID_DRUMS_VELOCITY_SOURCE_PEDAL;
						break;
					default:
						drums_mesg->velocity_source = CWIID_DRUMS_VELOCITY_SOURCE_NONE;
				}
				drums_mesg->velocity = 7 - (((uint8_t)data[3] & 0xE0) >> 5);
			} else {
				// cwiid_err(wiimote, "no velocity data TODO: FIXME");
				drums_mesg->velocity_source = CWIID_DRUMS_VELOCITY_SOURCE_NONE;
				drums_mesg->velocity = 0;
			}
			drums_mesg->buttons = ~((uint16_t)data[4]<<8 | (uint16_t)data[5]);
		}
		break;
	case CWIID_EXT_TURNTABLES:
		if (wiimote->state.rpt_mode & CWIID_RPT_TURNTABLES) {
			turntables_mesg = &ma->array[ma->count++].turntables_mesg;
			turntables_mesg->type = CWIID_MESG_TURNTABLES;
			turntables_mesg->stick[CWIID_X] = data[0] & CWIID_TURNTABLES_STICK_MAX;
			turntables_mesg->stick[CWIID_Y] = data[1] & CWIID_TURNTABLES_STICK_MAX;
			turntables_mesg->crossfader = ((uint8_t)data[2] & 0x1E)>>1;
			turntables_mesg->effect_dial = ((uint8_t)data[2] & 0x60)>>2 | 
                                           ((uint8_t)data[3] & 0xE0)>>5;
            int8_t left_x4 = (int8_t)(
							     ((uint8_t)data[3] & 0x1F)
							   | ((uint8_t)data[4] & 0x1)<<5
							 )<<2;
			turntables_mesg->left_turntable = left_x4 / 4;
			int8_t right_x4 = (int8_t)(
								  ((uint8_t)data[0] & 0xC0)>>3
								| ((uint8_t)data[1] & 0xC0)>>5
								| ((uint8_t)data[2] & 0x80)>>7
								| ((uint8_t)data[2] & 0x01)<<5
							  )<<2;
			turntables_mesg->right_turntable = right_x4 / 4;
			turntables_mesg->buttons =  ~(((uint16_t)data[4] & 0xFE)<<8 | (uint16_t)data[5]);
		}
		break;
	}

	return 0;
}

int process_read(struct wiimote *wiimote, unsigned char *data)
{
	struct rw_mesg rw_mesg;

	if (wiimote->rw_status != RW_READ) {
		cwiid_err(wiimote, "Received unexpected read report");
		return -1;
	}

	rw_mesg.type = RW_READ;
	rw_mesg.len = (data[0]>>4)+1;
	rw_mesg.error = data[0] & 0x0F;
	memcpy(&rw_mesg.data, data+3, rw_mesg.len);

	if (write(wiimote->rw_pipe[1], &rw_mesg, sizeof rw_mesg) !=
	  sizeof rw_mesg) {
		cwiid_err(wiimote, "RW pipe write error: %s", strerror(errno));
		return -1;
	}

	return 0;
}

int process_write(struct wiimote *wiimote, unsigned char *data)
{
	struct rw_mesg rw_mesg;

	if (wiimote->rw_status != RW_WRITE) {
		cwiid_err(wiimote, "Received unexpected write report");
		return -1;
	}

	rw_mesg.type = RW_WRITE;
	rw_mesg.error = data[0];

	if (write(wiimote->rw_pipe[1], &rw_mesg, sizeof rw_mesg) !=
	  sizeof rw_mesg) {
		cwiid_err(wiimote, "RW pipe write error: %s", strerror(errno));
		return -1;
	}

	return 0;
}
