/*
 * Copyright (c) 2018 Foundries.io
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOTA_PERIPHS_H__
#define FOTA_PERIPHS_H__

enum periph_device {
	DEV_IDX_MMA8652 = 0,
	DEV_IDX_APDS9960,
	DEV_IDX_EPD,
	DEV_IDX_NUMOF,
};

int get_hdc1010_val(struct sensor_value *val);
int get_mma8652_val(struct sensor_value *val);
int get_apds9960_val(struct sensor_value *val);
int set_led_state(u8_t id, bool state);
int periphs_init(void);

#endif	/* FOTA_PERIPHS_H__ */
