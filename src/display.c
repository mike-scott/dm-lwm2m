/*
 * Copyright (c) 2018 Foundries.io
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_MODULE_NAME fota_display
#define LOG_LEVEL CONFIG_FOTA_LOG_LEVEL

#include <logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr.h>
#include <gpio.h>
#include <sensor.h>
#include <display/cfb.h>
#include <net/lwm2m.h>
#include <settings/settings.h>

#include <stdio.h>

#include "periphs.h"
#include "product_id.h"

#define EDGE (GPIO_INT_EDGE | GPIO_INT_DOUBLE_EDGE)

#ifdef SW0_GPIO_FLAGS
#define PULL_UP SW0_GPIO_FLAGS
#else
#define PULL_UP 0
#endif

#define LINE_MAX 12

#if defined(CONFIG_SSD1673)
#define DISPLAY_DRIVER "SSD1673"
#else
#error Unsupported board
#endif

enum font_size {
	FONT_BIG = 0,
	FONT_MEDIUM = 1,
	FONT_SMALL = 2,
};

struct font_info {
	u8_t columns;
} fonts[] = {
	[FONT_BIG] =    { .columns = 12 },
	[FONT_MEDIUM] = { .columns = 16 },
	[FONT_SMALL] =  { .columns = 25 },
};

enum screen_ids {
	SCREEN_SENSORS = 0,
	SCREEN_MAIN,
	SCREEN_LAST,
};

#define LONG_PRESS_TIMEOUT K_SECONDS(1)

static struct device *epd_dev;
static bool pressed;
static u8_t screen_id = SCREEN_MAIN;
static struct device *gpio;
static struct k_delayed_work epd_work;
static struct k_delayed_work long_press_work;
static char str_buf[256];

static size_t print_line(enum font_size font_size, int row, const char *text,
			 size_t len, bool center)
{
	u8_t font_height, font_width;
	u8_t line[fonts[FONT_SMALL].columns + 1];
	int pad;

	cfb_framebuffer_set_font(epd_dev, font_size);

	len = MIN(len, fonts[font_size].columns);
	memcpy(line, text, len);
	line[len] = '\0';

	if (center) {
		pad = (fonts[font_size].columns - len) / 2;
	} else {
		pad = 0;
	}

	cfb_get_font_size(epd_dev, font_size, &font_width, &font_height);

	if (cfb_print(epd_dev, line, font_width * pad, font_height * row)) {
		LOG_INF("Failed to print a string\n");
	}

	return len;
}

static size_t get_len(enum font_size font, const char *text)
{
	const char *space = NULL;
	size_t i;

	for (i = 0; i <= fonts[font].columns; i++) {
		switch (text[i]) {
		case '\n':
		case '\0':
			return i;
		case ' ':
			space = &text[i];
			break;
		default:
			continue;
		}
	}

	/* If we got more characters than fits a line, and a space was
	 * encountered, fall back to the last space.
	 */
	if (space) {
		return space - text;
	}

	return fonts[font].columns;
}

void board_show_text(const char *text, bool center, s32_t duration)
{
	int i;

	cfb_framebuffer_set_font(epd_dev, 0);
	cfb_framebuffer_clear(epd_dev, false);

	for (i = 0; i < 3; i++) {
		size_t len;

		while (*text == ' ' || *text == '\n') {
			text++;
		}

		len = get_len(FONT_BIG, text);
		if (!len) {
			break;
		}

		text += print_line(FONT_BIG, i, text, len, center);
		if (!*text) {
			break;
		}
	}

	cfb_framebuffer_finalize(epd_dev);

	if (duration != K_FOREVER) {
		k_delayed_work_submit(&epd_work, duration);
	}
}

void board_refresh_display(void)
{
	k_delayed_work_submit(&epd_work, K_NO_WAIT);
}

void fio(void)
{
#if defined(CONFIG_NET_L2_OPENTHREAD)
	strncpy(str_buf, "FOUNDRIES.IO\nZmP LWM2M\nOPENTHREAD", sizeof(str_buf));
#else
	strncpy(str_buf, "FOUNDRIES.IO\nZmP LWM2M\nBLE", sizeof(str_buf));
#endif
	board_show_text(str_buf, true, K_FOREVER);
}

static int display_clear_cb(u16_t obj_inst_id)
{
	int ret = 0;
	int err = 0;
	LOG_INF("Clearing framebuffer\n");
	err = cfb_framebuffer_clear(epd_dev, true);
	if (err) {
					LOG_INF("Framebuffer clear error=%d\n", err);
					return err;
	}
	cfb_framebuffer_finalize(epd_dev);
	return ret;
}

/* post write hook */
static int display_text_cb(u16_t obj_inst_id, u8_t *data, u16_t data_len,
			   bool last_block, size_t total_size)
{
	int ret = 0;
	strncpy(str_buf, data, sizeof(str_buf));
	board_show_text(str_buf, true, K_FOREVER);
	return ret;
}

static void show_sensors_data(s32_t interval)
{
	struct sensor_value val[3];
	u8_t line = 0U;
	u16_t len = 0U;

	cfb_framebuffer_clear(epd_dev, false);

	len = snprintf(str_buf, sizeof(str_buf), "Client ID : %08x\n",
		       product_id_get()->number);
	print_line(FONT_SMALL, line++, str_buf, len, false);

	/* mma8652 */
	if (get_mma8652_val(val)) {
		goto _error_get;
	}

	len = snprintf(str_buf, sizeof(str_buf), "X :%10.3f\n",
		       sensor_value_to_double(&val[0]));
	print_line(FONT_SMALL, line++, str_buf, len, false);

	len = snprintf(str_buf, sizeof(str_buf), "Y :%10.3f\n",
		       sensor_value_to_double(&val[1]));
	print_line(FONT_SMALL, line++, str_buf, len, false);

	len = snprintf(str_buf, sizeof(str_buf), "Z :%10.3f\n",
		       sensor_value_to_double(&val[2]));
	print_line(FONT_SMALL, line++, str_buf, len, false);

	/* apds9960 */
	if (get_apds9960_val(val)) {
		goto _error_get;
	}

	len = snprintf(str_buf, sizeof(str_buf), "Ambient Light : %d\n", val[0].val1);
	print_line(FONT_SMALL, line++, str_buf, len, false);
	len = snprintf(str_buf, sizeof(str_buf), "Proximity: %d\n", val[1].val1);
	print_line(FONT_SMALL, line++, str_buf, len, false);

	cfb_framebuffer_finalize(epd_dev);

	k_delayed_work_submit(&epd_work, interval);

	return;

_error_get:
	LOG_INF("Failed to get sensor data or print a string\n");
}

static void epd_update(struct k_work *work)
{
	switch (screen_id) {
	case SCREEN_SENSORS:
		show_sensors_data(K_SECONDS(2));
		return;
	case SCREEN_MAIN:
	  settings_load();
		return;
	}
}

static void long_press(struct k_work *work)
{
	/* Treat as release so actual release doesn't send messages */
	pressed = false;
	screen_id = (screen_id + 1) % SCREEN_LAST;
	printk("Change screen to id = %d\n", screen_id);
	board_refresh_display();
}

static bool button_is_pressed(void)
{
	u32_t val;

	gpio_pin_read(gpio, SW0_GPIO_PIN, &val);

	return !val;
}

static void button_interrupt(struct device *dev, struct gpio_callback *cb,
			     u32_t pins)
{
	if (button_is_pressed() == pressed) {
		return;
	}

	pressed = !pressed;
	printk("Button %s\n", pressed ? "pressed" : "released");

	if (pressed) {
		k_delayed_work_submit(&long_press_work, LONG_PRESS_TIMEOUT);
		return;
	}

	k_delayed_work_cancel(&long_press_work);

	/* Short press for views */
	switch (screen_id) {
	case SCREEN_SENSORS:
	case SCREEN_MAIN:
	default:
		return;
	}
}

static int configure_button(void)
{
        static struct gpio_callback button_cb;

        gpio = device_get_binding(SW0_GPIO_CONTROLLER);
        if (!gpio) {
                return -ENODEV;
        }

        gpio_pin_configure(gpio, SW0_GPIO_PIN,
                           (GPIO_DIR_IN | GPIO_INT |  PULL_UP | EDGE));

        gpio_init_callback(&button_cb, button_interrupt, BIT(SW0_GPIO_PIN));
        gpio_add_callback(gpio, &button_cb);

        gpio_pin_enable_callback(gpio, SW0_GPIO_PIN);

	k_delayed_work_init(&epd_work, epd_update);
	k_delayed_work_init(&long_press_work, long_press);

        return 0;
}

int init_display(void)
{
	int ret;

	epd_dev = device_get_binding(DISPLAY_DRIVER);
	if (epd_dev == NULL) {
		LOG_INF("Display driver not found\n");
		return -ENODEV;
	}

	if (cfb_framebuffer_init(epd_dev)) {
		LOG_INF("Framebuffer initialization failed\n");
		return -EIO;
	}

        if (configure_button()) {
                LOG_INF("Failed to configure button\n");
                return -EIO;
        }

	cfb_framebuffer_clear(epd_dev, true);
	fio();
	ret = lwm2m_engine_create_obj_inst("3341/0");
	if (ret < 0) {
		goto fail;
	}

	ret = lwm2m_engine_register_post_write_callback("3341/0/5527",
							display_text_cb);
	if (ret < 0) {
		goto fail;
	}

        ret = lwm2m_engine_register_exec_callback("3341/0/5530",
                                                  display_clear_cb);
        if (ret < 0) {
                goto fail;
        }

	return 0;

fail:
	return ret;
}

#if defined(CONFIG_LWM2M_PERSIST_SETTINGS)
void display_text_persist(void)
{
	/* save display state */
	lwm2m_engine_set_persist("3341/0/5527");
}
#endif
