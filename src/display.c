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
#include <display/cfb.h>
#include <net/lwm2m.h>

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

static struct device *epd_dev;
static bool pressed;
static struct device *gpio;
static struct k_delayed_work epd_work;
static char buf[36];

static size_t print_line(int row, const char *text, size_t len, bool center)
{
	u8_t font_height, font_width;
	u8_t line[LINE_MAX + 1];
	int pad;

	len = MIN(len, LINE_MAX);
	memcpy(line, text, len);
	line[len] = '\0';

	if (center) {
		pad = (LINE_MAX - len) / 2;
	} else {
		pad = 0;
	}

	cfb_get_font_size(epd_dev, 0, &font_width, &font_height);

	if (cfb_print(epd_dev, line, font_width * pad, font_height * row)) {
		printk("Failed to print a string\n");
	}

	return len;
}

static size_t get_len(const char *text)
{
	const char *space = NULL;
	size_t i;

	for (i = 0; i <= LINE_MAX; i++) {
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

	return LINE_MAX;
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

		len = get_len(text);
		if (!len) {
			break;
		}

		text += print_line(i, text, len, center);
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
	strncpy(buf, "FOUNDRIES.IO\nZmP LWM2M\nOPENTHREAD", sizeof(buf));
#else
	strncpy(buf, "FOUNDRIES.IO\nZmP LWM2M\nBLE", sizeof(buf));
#endif
	board_show_text(buf, true, K_FOREVER);
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
	strncpy(buf, data, sizeof(buf));
	board_show_text(buf, true, K_FOREVER);
	return ret;
}

static void display_serial(struct k_work *work)
{
        static char serial[10];

	pressed = !pressed;

        if (pressed) {
		LOG_INF("Displaying Serial Number");
	        snprintk(serial, sizeof(serial), "SN:%08x",
				product_id_get()->number);
		board_show_text(serial, true, K_FOREVER);
        } else {
		LOG_INF("Displaying Buffer Text");
		board_show_text(buf, true, K_FOREVER);
	}
}

static void button_interrupt(struct device *dev, struct gpio_callback *cb,
                             u32_t pins)
{
	board_refresh_display();
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

	k_delayed_work_init(&epd_work, display_serial);

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
