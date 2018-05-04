/*
 * Copyright (c) 2016-2017 Linaro Limited
 * Copyright (c) 2018 Open Source Foundries Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define SYS_LOG_DOMAIN "fota/main"
#define SYS_LOG_LEVEL CONFIG_SYS_LOG_FOTA_LEVEL
#include <logging/sys_log.h>

#include <zephyr.h>
#include <sensor.h>
#include <board.h>
#include <gpio.h>
#include <net/lwm2m.h>
#include <tc_util.h>

/* Local helpers and functions */
#include "tstamp_log.h"
#include "app_work_queue.h"
#include "product_id.h"
#include "lwm2m.h"
#include "light_control.h"
#if defined(CONFIG_LWM2M_IPSO_SYSTEM_LOG)
#include "lwm2m_system_log.h"
#endif

/* Defines and configs for the IPSO elements */
#define TEMP_DEV		"fota-temp"
#define TEMP_CHAN		SENSOR_CHAN_DIE_TEMP

static struct device *die_dev;
static struct float32_value temp_float;

static int read_temperature(struct device *temp_dev,
			    struct float32_value *float_val)
{
	__unused const char *name = temp_dev->config->name;
	struct sensor_value temp_val;
	int ret;

	ret = sensor_sample_fetch(temp_dev);
	if (ret) {
		SYS_LOG_ERR("%s: I/O error: %d", name, ret);
		return ret;
	}

	ret = sensor_channel_get(temp_dev, TEMP_CHAN, &temp_val);
	if (ret) {
		SYS_LOG_ERR("%s: can't get data: %d", name, ret);
		return ret;
	}

	SYS_LOG_DBG("%s: read %d.%d C",
			name, temp_val.val1, temp_val.val2);
	float_val->val1 = temp_val.val1;
	float_val->val2 = temp_val.val2;

	return 0;
}

static void *temp_read_cb(u16_t obj_inst_id, size_t *data_len)
{
	/* Only object instance 0 is currently used */
	if (obj_inst_id != 0) {
		*data_len = 0;
		return NULL;
	}

	/*
	 * No need to check if read was successful, just reuse the
	 * previous value which is already stored at temp_float.
	 * This is because there is currently no way to report read_cb
	 * failures to the LWM2M engine.
	 */
	read_temperature(die_dev, &temp_float);
	lwm2m_engine_set_float32("3303/0/5700", &temp_float);
	*data_len = sizeof(temp_float);

	return &temp_float;
}

static int init_temp_device(void)
{
	die_dev = device_get_binding(TEMP_DEV);
	SYS_LOG_INF("%s on-die temperature sensor %s",
			die_dev ? "Found" : "Did not find",
			TEMP_DEV);

	if (!die_dev) {
		SYS_LOG_ERR("No temperature device found.");
		return -ENODEV;
	}

	return 0;
}

void main(void)
{
	tstamp_hook_install();
	app_wq_init();

	SYS_LOG_INF("Open Source Foundries FOTA LWM2M example application");
	SYS_LOG_INF("Device: %s, Serial: %x",
		    product_id_get()->name, product_id_get()->number);

	TC_START("Running Built in Self Test (BIST)");

	TC_PRINT("Initializing LWM2M IPSO Temperature Sensor\n");
	if (init_temp_device()) {
		_TC_END_RESULT(TC_FAIL, "init_temp_device");
		TC_END_REPORT(TC_FAIL);
		return;
	}
	lwm2m_engine_create_obj_inst("3303/0");
	lwm2m_engine_register_read_callback("3303/0/5700", temp_read_cb);
	lwm2m_engine_set_string("3303/0/5701", "Cel");
	_TC_END_RESULT(TC_PASS, "init_temp_device");

	TC_PRINT("Initializing IPSO Light Control\n");
	if (init_light_control()) {
		_TC_END_RESULT(TC_FAIL, "init_light_control");
		TC_END_REPORT(TC_FAIL);
		return;
	}
	_TC_END_RESULT(TC_PASS, "init_light_control");

#if defined(CONFIG_LWM2M_IPSO_SYSTEM_LOG)
	TC_PRINT("Initializing IPSO System Log\n");
	if (init_lwm2m_system_log()) {
		_TC_END_RESULT(TC_FAIL, "init_lwm2m_system_log");
		TC_END_REPORT(TC_FAIL);
		return;
	}
	_TC_END_RESULT(TC_PASS, "init_lwm2m_system_log");
#endif

	TC_END_REPORT(TC_PASS);

	if (lwm2m_init()) {
		return;
	}

	/*
	 * From this point on, just handle work.
	 */
	app_wq_run();
}
