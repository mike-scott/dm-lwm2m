/*
 * Copyright (c) 2018 Open Source Foundries Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define SYS_LOG_DOMAIN "fota/syslog"
#define SYS_LOG_LEVEL CONFIG_SYS_LOG_FOTA_LEVEL
#include <logging/sys_log.h>

#include <zephyr.h>
#include <stdlib.h>
#include <string.h>
#include <device.h>
#include <fcb.h>
#include <shell/shell.h>
#include <misc/printk.h>

#include "tstamp_log.h"

#define SYSLOG_SHELL_MODULE "syslog"

static int syslog_shell_cmd_disable(int argc, char *argv[])
{
	set_syslog_enable(false);

	return 0;
}

static int syslog_shell_cmd_enable(int argc, char *argv[])
{
	set_syslog_enable(true);

	return 0;
}

static int syslog_shell_walk_cb(struct fcb_entry_ctx *entry_ctx, void *arg)
{
	char *msg = (char *)(FLASH_AREA_SYSLOG_OFFSET +
			     FCB_ENTRY_FA_DATA_OFF(entry_ctx->loc));

	if (msg) {
		printk("> %s", msg);
	}

	return 0;
}

static int syslog_shell_cmd_read(int argc, char *argv[])
{
	fcb_walk(get_syslog_fcb(), NULL, syslog_shell_walk_cb, NULL);

	return 0;
}

static int syslog_shell_cmd_reset(int argc, char *argv[])
{
	return syslog_reset();
}

static struct shell_cmd syslog_commands[] = {
	/* Keep the commands in alphabetical order */
	{ "disable", syslog_shell_cmd_disable,
		"\n\tDisable storage of syslog messages" },
	{ "enable", syslog_shell_cmd_enable,
		"\n\tEnable storage of syslog messages" },
	{ "read", syslog_shell_cmd_read,
		"\n\tRead the contents of the syslog flash buffer" },
	{ "reset", syslog_shell_cmd_reset,
		"\n\tReset the contents of the syslog flash buffer" },
	{ NULL, NULL, NULL }
};

SHELL_REGISTER(SYSLOG_SHELL_MODULE, syslog_commands);
