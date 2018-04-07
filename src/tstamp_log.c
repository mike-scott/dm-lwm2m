/*
 * Copyright (c) 2017 Linaro Limited
 * Copyright (c) 2010, 2013-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Even though we don't actually log from this file, without this
 * define, sys_log.h will think it's inactive in this compilation
 * unit.
 */
#define SYS_LOG_LEVEL SYS_LOG_LEVEL_ERROR


#include <logging/sys_log.h>
#include <misc/printk.h>
#include <zephyr.h>

#include <stdarg.h>
#include "tstamp_log.h"

#if defined(CONFIG_FOTA_SYSLOG_SUPPORT)
#include <string.h>
#include <flash_map.h>
#include <fcb.h>

#define SYSLOG_AREA_ID	0
#define SYSLOG_MAGIC	0x12345678

#define WRITE_ALIGN(n,a)	((((n) + (a - 1)) / a) * a)
#define TS_MAX_LEN		15

static struct fcb syslog_fcb;
static u8_t syslog_enabled;
static u8_t syslog_changed;

const struct flash_area syslog_areas[] = {
	{
		.fa_id = 0,
		.fa_device_id = SOC_FLASH_0_ID,
		.fa_off = FLASH_AREA_SYSLOG_OFFSET,
		.fa_size = FLASH_AREA_SYSLOG_SIZE,
	},
};

/* TODO: define sectors inside syslog_area dynamically */
struct flash_sector syslog_sectors[CONFIG_FOTA_SYSLOG_SECTOR_COUNT];

/* flash_map variables for flash_map storage driver */
const int flash_map_entries = ARRAY_SIZE(syslog_areas);
const struct flash_area *flash_map = syslog_areas;

u8_t get_syslog_changed(void)
{
	u8_t temp = syslog_changed;

	syslog_changed = 0;
	return temp;
}

u8_t get_syslog_enable(void)
{
	return syslog_enabled;
}

void set_syslog_enable(u8_t enabled)
{
	syslog_enabled = enabled;
}

struct fcb *get_syslog_fcb(void)
{
	return &syslog_fcb;
}

int syslog_reset(void)
{
	const struct flash_area *area;
	int i, ret = 0;
	u8_t temp_enabled = syslog_enabled;

	/* disable syslog temporarily */
	if (syslog_enabled) {
		syslog_enabled = false;
	}

	ret = flash_area_open(syslog_areas[SYSLOG_AREA_ID].fa_id, &area);
	if (ret < 0) {
		return ret;
	}

	for (i = 0; i < CONFIG_FOTA_SYSLOG_SECTOR_COUNT; i++) {
		ret = flash_area_erase(area, syslog_sectors[i].fs_off,
				       syslog_sectors[i].fs_size);
		if (ret < 0) {
			break;
		}
	}

	ret = fcb_init(SYSLOG_AREA_ID, &syslog_fcb);

	/* re-enabled syslog if needed */
	if (ret == 0 && temp_enabled) {
		syslog_enabled = true;
	}

	return ret;
}
#endif

static void tstamp_log_fn(const char *fmt, ...)
{
	va_list ap;
	u32_t up_ms = k_uptime_get_32();
#if defined(CONFIG_FOTA_SYSLOG_SUPPORT)
	/* HACK: fixup for dynamic length */
	static char output_ms[TS_MAX_LEN];
	static char output_line[WRITE_ALIGN(128, FLASH_WRITE_BLOCK_SIZE)];
	struct fcb_entry syslog_entry;
	int ret = 0;

	if (syslog_enabled) {
		va_start(ap, fmt);
		/* calculate the TS */
		snprintk(output_ms, TS_MAX_LEN, "[%07u]", up_ms);
		/* leave room for adding time stamp */
		vsnprintk(output_line + strlen(output_ms) + 1,
			  sizeof(output_line) - strlen(output_ms) - 1, fmt, ap);
		va_end(ap);
		snprintk(output_line, TS_MAX_LEN, "%s", output_ms);
		output_line[strlen(output_ms)] = ' ';

		ret = fcb_append(&syslog_fcb, strlen(output_line) + 1,
				 &syslog_entry);
		if (ret == FCB_ERR_NOSPACE) {
			ret = fcb_rotate(&syslog_fcb);
			if (ret < 0) {
				printk("[ERR] %s: fcb_rotate: %d\n",
				       __func__, ret);
				goto error;
			}

			ret = fcb_append(&syslog_fcb, strlen(output_line) + 1,
					 &syslog_entry);
			if (ret < 0) {
				printk("[ERR] %s: fcb_append: %d\n",
				       __func__, ret);
				goto error;
			}
		} else if (ret < 0) {
			printk("[ERR] %s: fcb_append: %d\n", __func__, ret);
			goto error;
		}

		ret = flash_area_write(syslog_fcb.fap,
				 FCB_ENTRY_FA_DATA_OFF(syslog_entry),
				 output_line,
				 WRITE_ALIGN(strlen(output_line) + 1,
					     FLASH_WRITE_BLOCK_SIZE));
		if (ret < 0) {
			printk("[ERR] %s: flash_area_write: %d\n",
			       __func__, ret);
			goto error;
		}

		ret = fcb_append_finish(&syslog_fcb, &syslog_entry);
		if (ret < 0) {
			printk("[ERR] %s: fcb_append_finish: %d\n",
			       __func__, ret);
		}

error:
		printk("%s", output_line);
	} else
#endif
	{
		printk("[%07u] ", up_ms);

		va_start(ap, fmt);
		vprintk(fmt, ap);
		va_end(ap);
	}
}

void tstamp_hook_install(void)
{
#if defined(CONFIG_FOTA_SYSLOG_SUPPORT)
	int ret = 0, i;

	for (i = 0; i < CONFIG_FOTA_SYSLOG_SECTOR_COUNT; i++) {
		syslog_sectors[i].fs_off = i * CONFIG_FOTA_SYSLOG_SECTOR_SIZE;
		syslog_sectors[i].fs_size = CONFIG_FOTA_SYSLOG_SECTOR_SIZE;
	}

	memset(&syslog_fcb, 0, sizeof(syslog_fcb));
	syslog_fcb.f_sectors = syslog_sectors;
	syslog_fcb.f_sector_cnt = CONFIG_FOTA_SYSLOG_SECTOR_COUNT;
	syslog_fcb.f_scratch_cnt = 0;
	syslog_fcb.f_magic = SYSLOG_MAGIC;

	ret = fcb_init(SYSLOG_AREA_ID, &syslog_fcb);
	if (ret < 0) {
		SYS_LOG_WRN("Failed to open syslog storage! Erasing.");
		ret = syslog_reset();
		if (ret < 0) {
			SYS_LOG_ERR("Error resetting syslog storage! (%d)",
				    ret);
			return;
		}
	}

	syslog_enabled = IS_ENABLED(CONFIG_FOTA_SYSLOG_ENABLED);
#endif

	syslog_hook_install(tstamp_log_fn);
}
