/*
 * Copyright (c) 2018 Foundries.io
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOTA_DISPLAY_H__
#define FOTA_DISPLAY_H__

int init_display(void);
void board_refresh_display(void);
void display_text_persist(void);

#endif	/* FOTA_DISPLAY_H__ */
