/*
 * Mouse
 *
 * Copyright (c) 2023 Mirco Müller <macslow@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Mouse
 *
 * Support for mouse/pointer-devices (usb-mice, trackpoints,
 * trackpads etc.) and text copy&paste à la gpm is implemented here.
 *
 * You can get x- and y-coordinates (in character-cells) and check if
 * LMB, MMB or RMB are up, down, pressed or released. You can also check
 * for single- or double-click of LMB, MMB or RMB.
 *
 * There should only ever be one kmscon_mouse_info structure be
 * created via kmscon_mouse_init() in all of kmscon. It will handle
 * everything that's plugged into the system via the Linux kernels
 * input sub-system.
 *
 * It is written in a way to be easy to read and understand, not waste
 * cpu-cycles and just support enough features in order to implement
 * gpm-like cursor- and copy&paste-functionality. Take note that scroll-
 * -wheels are currently NOT supported.
 *
 * Do not use this in a general UI-toolkit manner like mouse-support in
 * Qt, gtk+ or the like.
 */

#ifndef KMSCON_MOUSE_H
#define KMSCON_MOUSE_H

#include <libtsm.h>
#include <stdint.h>
#include <time.h>

#include "eloop.h"

#define MAX_DEVNAME_SIZE 16
#define DEVNAME_TEMPLATE "/dev/input/mice"
#define BUTTON_MASK_LEFT      0x01
#define BUTTON_MASK_RIGHT     0x02
#define BUTTON_MASK_MIDDLE    0x04
#define MOTION_SCALE     2
#define BUFFER_SIZE      3

#define KMSCON_MOUSE_BUTTON_LEFT   0
#define KMSCON_MOUSE_BUTTON_MIDDLE 1
#define KMSCON_MOUSE_BUTTON_RIGHT  2

typedef enum {
	KMSCON_MOUSE_BUTTON_UNDEFINED = -1,
	KMSCON_MOUSE_BUTTON_PRESSED   = 0,
	KMSCON_MOUSE_BUTTON_RELEASED  = 1,
	KMSCON_MOUSE_BUTTON_DOWN      = 2,
	KMSCON_MOUSE_BUTTON_UP        = 3
} kmscon_mouse_button_state;

struct kmscon_selection_info {
	char* buffer;
	int buffer_length;
};

struct kmscon_mouse_info {
	struct kmscon_selection_info* selection;

	struct ev_timer* query_timer;
	struct itimerspec query_timer_spec;

	struct ev_timer* hide_timer;
	struct itimerspec hide_timer_spec;

	int device;
	float x;
	float y;
	int hide;

	kmscon_mouse_button_state state[3];
	int clicked[3];
	int dbl_clicked[3];

	struct timeval last_up;
	struct timeval last_down;
	struct timeval click;

	struct uterm_display* disp;
	struct kmscon_text* txt;
	struct kmscon_seat* seat;
};

struct kmscon_mouse_info* kmscon_mouse_init(struct ev_eloop* eloop,
                                            struct kmscon_seat* seat);
void kmscon_mouse_cleanup(struct kmscon_mouse_info* mouse);

void kmscon_mouse_set_mapping(struct kmscon_mouse_info* mouse,
							  struct uterm_display* disp,
							  struct kmscon_text* txt);

int kmscon_mouse_get_x(struct kmscon_mouse_info* mouse);
int kmscon_mouse_get_y(struct kmscon_mouse_info* mouse);

int kmscon_mouse_is_clicked(struct kmscon_mouse_info* mouse, int button);
void kmscon_mouse_clear_clicked(struct kmscon_mouse_info* mouse, int button);
int kmscon_mouse_is_dbl_clicked(struct kmscon_mouse_info* mouse, int button);
void kmscon_mouse_clear_dbl_clicked(struct kmscon_mouse_info* mouse, int button);

int kmscon_mouse_is_up(struct kmscon_mouse_info* mouse, int button);
int kmscon_mouse_is_down(struct kmscon_mouse_info* mouse, int button);
int kmscon_mouse_is_released(struct kmscon_mouse_info* mouse, int button);
int kmscon_mouse_is_pressed(struct kmscon_mouse_info* mouse, int button);
int kmscon_mouse_is_hidden(struct kmscon_mouse_info* mouse);

void kmscon_mouse_selection_copy(struct kmscon_mouse_info* mouse,
								 struct tsm_screen *console);

int kmscon_mouse_is_selection_empty(struct kmscon_mouse_info* mouse);

#endif /* KMSCON_MOUSE_H */
