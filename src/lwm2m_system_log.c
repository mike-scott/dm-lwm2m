/*
 * Copyright (c) 2018 Open Source Foundries Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define SYS_LOG_DOMAIN "fota/lwm2m-syslog"
#define SYS_LOG_LEVEL CONFIG_SYS_LOG_FOTA_LEVEL
#include <logging/sys_log.h>

#include <zephyr.h>
#include <board.h>
#include <gpio.h>
#include <fcb.h>
#include <net/lwm2m.h>

#include "tstamp_log.h"

#define SYSLOG_NAME	"syslog"

/*
 * TODO: Currently this needs to be small / fixed size.  LwM2M engine needs
 *       to support outgoing blockwise transfer on large reads.
 */
static char syslog_buffer[1024];

struct syslog_reader {
	char *last_read;
	char *walk_start;
};

static struct syslog_reader log_reader;

static int syslog_walk_cb(struct fcb_entry_ctx *entry_ctx, void *arg)
{
	struct syslog_reader *reader = (struct syslog_reader *)arg;
	char *msg = (char *)(FLASH_AREA_SYSLOG_OFFSET +
			     FCB_ENTRY_FA_DATA_OFF(entry_ctx->loc));

	if (msg) {
		if (reader && reader->walk_start == 0) {
			reader->walk_start = msg;
		}

		/* determine if we should skip entry based on last_read */
		if (reader) {
			/* moving forward */
			if (reader->last_read >= reader->walk_start) {
				if (msg >= reader->walk_start &&
				    msg <= reader->last_read) {
					return 0;
				}
			/* wrapped */
			} else {
				if (msg >= reader->walk_start &&
				    msg <= reader->last_read) {
					return 0;
				}
			}
		}

		if (strlen(msg) >= sizeof(syslog_buffer) -
				   strlen(syslog_buffer)) {
			size_t hbuf_len;

			/* shift data 1/2 way down the buffer */
			hbuf_len = sizeof(syslog_buffer) / 2;
			memcpy(syslog_buffer, syslog_buffer + hbuf_len,
			       hbuf_len);
			memset(syslog_buffer + hbuf_len, 0, hbuf_len);
		}

		strncat(syslog_buffer, msg, sizeof(syslog_buffer));

		/* assign last_read */
		if (reader) {
			reader->last_read = msg;
		}
	}

	return 0;
}

static void *log_readall_cb(u16_t obj_inst_id, size_t *data_len)
{
	/* Only object instance 0 is currently used */
	if (obj_inst_id != 0) {
		*data_len = 0;
		return NULL;
	}

	syslog_buffer[0] = '\0';

	/* specify NULL for arg to read entire log */
	fcb_walk(get_syslog_fcb(), NULL, syslog_walk_cb, NULL);

	*data_len = strlen(syslog_buffer);
	return syslog_buffer;
}

static void *log_read_cb(u16_t obj_inst_id, size_t *data_len)
{
	/* Only object instance 0 is currently used */
	if (obj_inst_id != 0) {
		*data_len = 0;
		return NULL;
	}

	syslog_buffer[0] = '\0';

	log_reader.walk_start = 0;
	fcb_walk(get_syslog_fcb(), NULL, syslog_walk_cb, &log_reader);

	*data_len = strlen(syslog_buffer);
	if (*data_len == 0) {
		*data_len = 1;
	}

	return syslog_buffer;
}

static int log_enabled_postwrite_cb(u16_t obj_inst_id,
				    u8_t *data, u16_t data_len,
				    bool last_block, size_t total_size)
{
	set_syslog_enable((u8_t)*data);

	return 0;
}

int init_lwm2m_system_log(void)
{
	int ret;

	ret = lwm2m_engine_create_obj_inst("10259/0");
	if (ret < 0) {
		goto fail;
	}

	lwm2m_engine_set_string("10259/0/0", SYSLOG_NAME);
	lwm2m_engine_set_bool("10259/0/3", (bool)get_syslog_enable());
	ret = lwm2m_engine_register_read_callback("10259/0/1", log_readall_cb);
	if (ret < 0) {
		goto fail;
	}

	ret = lwm2m_engine_register_read_callback("10259/0/2", log_read_cb);
	if (ret < 0) {
		goto fail;
	}

	ret = lwm2m_engine_register_post_write_callback("10259/0/3",
							log_enabled_postwrite_cb);
	if (ret < 0) {
		goto fail;
	}

	return 0;

fail:
	return ret;
}
