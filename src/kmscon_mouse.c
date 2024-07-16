#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "kmscon_mouse.h"
#include "kmscon_seat.h"
#include "shl_log.h"
#include "text.h"

// utility functions for internal (kmscon_mouse) use only
int mouse_ready_to_read(int handle);
void mouse_hide_timer_cb(struct ev_timer *timer, uint64_t num, void *data);
void handle_clicks (struct kmscon_mouse_info* mouse, int button);
void update_mouse_button_state (struct kmscon_mouse_info* mouse,
                                int button,
                                char button_mask,
                                char buffer);
void mouse_query_timer_cb(struct ev_timer *timer, uint64_t num, void *data);

// public API
struct kmscon_mouse_info* kmscon_mouse_init(struct ev_eloop* eloop,
                                            struct kmscon_seat* seat)
{
	struct kmscon_mouse_info* mouse;
	size_t size = sizeof(struct kmscon_mouse_info);
	mouse = (struct kmscon_mouse_info*) calloc(1, size);
	if (!mouse)
		goto err;

	mouse->seat = seat;
	mouse->state[KMSCON_MOUSE_BUTTON_LEFT] = KMSCON_MOUSE_BUTTON_RELEASED;
	mouse->state[KMSCON_MOUSE_BUTTON_MIDDLE] = KMSCON_MOUSE_BUTTON_RELEASED;
	mouse->state[KMSCON_MOUSE_BUTTON_RIGHT] = KMSCON_MOUSE_BUTTON_RELEASED;
	gettimeofday(&mouse->last_up, NULL);
	gettimeofday(&mouse->last_down, NULL);
	gettimeofday(&mouse->click, NULL);

	size = sizeof(struct kmscon_selection_info);
	mouse->selection = (struct kmscon_selection_info*) calloc(1, size);
	if (!mouse->selection)
		goto err;

	int use_mouse = true;
	char devname[MAX_DEVNAME_SIZE];
	int length = sprintf (devname, DEVNAME_TEMPLATE);
	if (length > MAX_DEVNAME_SIZE) {
		log_warn ("Error while creating device string!\n");
		use_mouse = false;
	}

	mouse->device = open (devname, O_RDONLY | O_NONBLOCK);
	if (mouse->device < 0) {
		log_warn ("Error while opening device '%s'!\n", devname);
		use_mouse = false;
	}

	if (use_mouse) {
		mouse->hide = true;

		// mouse-query timer
		mouse->query_timer_spec.it_interval.tv_sec  = 0;
		mouse->query_timer_spec.it_interval.tv_nsec = 10*1000*1000;
		mouse->query_timer_spec.it_value.tv_sec  = 0;
		mouse->query_timer_spec.it_value.tv_nsec = 10*1000*1000;

		int ret = ev_timer_new(&mouse->query_timer,
							   &mouse->query_timer_spec,
							   mouse_query_timer_cb,
							   mouse,
							   NULL,
							   NULL);
		if (ret) {
			log_error("cannot create mouse-query timer: %d", ret);
			goto err;
		}
		ev_timer_enable(mouse->query_timer);

		ret = ev_eloop_add_timer(eloop, mouse->query_timer);
		if (ret) {
			log_error("cannot add mouse-query timer to event-loop: %d", ret);
			goto err;
		}

		// mouse-hide timer
		mouse->hide_timer_spec.it_interval.tv_sec  = 5;
		mouse->hide_timer_spec.it_interval.tv_nsec = 0;
		mouse->hide_timer_spec.it_value.tv_sec  = 5;
		mouse->hide_timer_spec.it_value.tv_nsec = 0;

		ret = ev_timer_new(&mouse->hide_timer,
						   &mouse->hide_timer_spec,
						   mouse_hide_timer_cb,
						   mouse,
						   NULL,
						   NULL);
		if (ret) {
			log_error("cannot create mouse-hide timer: %d", ret);
			goto err;
		}
		ev_timer_enable(mouse->hide_timer);

		ret = ev_eloop_add_timer(eloop, mouse->hide_timer);
		if (ret) {
			log_error("cannot add mouse-hide timer to event-loop: %d", ret);
			goto err;
		}
	}

	return mouse;

err:
	kmscon_mouse_cleanup(mouse);
	return NULL;
}

void kmscon_mouse_cleanup(struct kmscon_mouse_info* mouse)
{
	if (mouse) {
		if (mouse->selection) {
			free(mouse->selection);
		}
		free(mouse);
	}
}

void kmscon_mouse_set_mapping(struct kmscon_mouse_info* mouse,
                              struct uterm_display* disp,
                              struct kmscon_text* txt)
{
	if (mouse && disp && txt) {
		mouse->disp = disp;
		mouse->txt = txt;
	}
}

int kmscon_mouse_get_x(struct kmscon_mouse_info* mouse)
{
	int x = 0;
	struct uterm_mode *mode = uterm_display_get_current(mouse->disp);
	int sw = uterm_mode_get_width(mode);
	int sh = uterm_mode_get_height(mode);
	float fw = mouse->txt->font->attr.width;

	if (mouse && mouse->disp && mouse->txt) {

		if (mouse->txt->orientation == ORIENTATION_NORMAL ||
			mouse->txt->orientation == ORIENTATION_INVERTED) {

			float pixel_w = 2.f / (float) sw;
			float cursor_x = (1.f + mouse->x)/pixel_w;
			cursor_x = cursor_x - fmodf(cursor_x, 1.f);
			x = (int) cursor_x/fw;
		}

		if (mouse->txt->orientation == ORIENTATION_RIGHT ||
			mouse->txt->orientation == ORIENTATION_LEFT) {

			float pixel_w = 2.f / (float) sh;
			float cursor_x = (1.f + mouse->x)/pixel_w;
			cursor_x = cursor_x - fmodf(cursor_x, 1.f);
			x = (int) cursor_x/fw;
		}

		x = (x >= mouse->txt->cols) ? mouse->txt->cols - 1 : x;
	}

	return x;
}

int kmscon_mouse_get_y(struct kmscon_mouse_info* mouse)
{
	int y = 0;
	struct uterm_mode *mode = uterm_display_get_current(mouse->disp);
	int sw = uterm_mode_get_width(mode);
	int sh = uterm_mode_get_height(mode);
	float fh = mouse->txt->font->attr.height;

	if (mouse && mouse->disp && mouse->txt) {

		if (mouse->txt->orientation == ORIENTATION_NORMAL ||
			mouse->txt->orientation == ORIENTATION_INVERTED) {

			float pixel_h = 2.f / (float) sh;
			float cursor_y = fabsf(-1.f + mouse->y)/pixel_h;
			cursor_y = cursor_y - fmodf(cursor_y, 1.f);
			y = (int) cursor_y/fh;
		}

		if (mouse->txt->orientation == ORIENTATION_RIGHT ||
			mouse->txt->orientation == ORIENTATION_LEFT) {

			float pixel_h = 2.f / (float) sw;
			float cursor_y = fabsf(-1.f + mouse->y)/pixel_h;
			cursor_y = cursor_y - fmodf(cursor_y, 1.f);
			y = (int) cursor_y/fh;
		}

		y = (y >= mouse->txt->rows) ? mouse->txt->rows - 1 : y;
	}

	return y;
}

int kmscon_mouse_is_clicked(struct kmscon_mouse_info* mouse,
                            int button)
{
	if (mouse && button <= 3 && button >= 0)
		return mouse->clicked[button];

	return 0;
}

void kmscon_mouse_clear_clicked(struct kmscon_mouse_info* mouse,
                                int button)
{
	if (mouse) {
		mouse->clicked[button] = false;
	}
}

int kmscon_mouse_is_dbl_clicked(struct kmscon_mouse_info* mouse,
                                int button)
{
	if (mouse && button <= 3 && button >= 0)
		return mouse->dbl_clicked[button];

	return 0;
}

void kmscon_mouse_clear_dbl_clicked(struct kmscon_mouse_info* mouse,
                                    int button)
{
	if (mouse) {
		mouse->dbl_clicked[button] = false;
	}
}

int kmscon_mouse_is_up(struct kmscon_mouse_info* mouse,
                       int button)
{
	if (mouse && button <= 3 && button >= 0)
		return (mouse->state[button] == KMSCON_MOUSE_BUTTON_UP);

	return 0;
}

int kmscon_mouse_is_down(struct kmscon_mouse_info* mouse,
                         int button)
{
	if (mouse && button <= 3 && button >= 0)
		return (mouse->state[button] == KMSCON_MOUSE_BUTTON_DOWN);

	return 0;
}

int kmscon_mouse_is_released(struct kmscon_mouse_info* mouse,
                             int button)
{
	if (mouse && button <= 3 && button >= 0)
		return (mouse->state[button] == KMSCON_MOUSE_BUTTON_RELEASED);

	return 0;
}

int kmscon_mouse_is_pressed(struct kmscon_mouse_info* mouse,
                            int button)
{
	if (mouse && button <= 3 && button >= 0)
		return (mouse->state[button] == KMSCON_MOUSE_BUTTON_PRESSED);

	return 0;
}

int kmscon_mouse_is_hidden(struct kmscon_mouse_info* mouse)
{
	if (mouse)
		return mouse->hide;

	return 0;
}

void kmscon_mouse_selection_copy(struct kmscon_mouse_info* mouse,
                                 struct tsm_screen *console)
{
	if (mouse && console) {
		int length = tsm_screen_selection_copy(console,
											   &mouse->selection->buffer);
		mouse->selection->buffer_length = length;
	}
}

int kmscon_mouse_is_selection_empty(struct kmscon_mouse_info* mouse)
{
	if (!mouse || !mouse->selection)
		return true;

	if (mouse->selection->buffer_length == 0)
		return true;
	else
		return false;
}

// internal API/utility functions
int mouse_ready_to_read(int handle)
{
	const struct timespec timeout = {0, 3*1000*1000};
	fd_set readfds;

	FD_ZERO(&readfds);
	FD_SET(handle, &readfds);
	int result = pselect(handle + 1, &readfds, NULL, NULL, &timeout, NULL);
	if (result == -1) {
		log_error("There was an error trying to check for available mouse-data.");
		return 0;
	} else if (result == 0) {
		return 0;
	} else {
		result = FD_ISSET(handle, &readfds);
		if (result) {
			return 1;
		}
	}

	return 0;
}

void mouse_hide_timer_cb(struct ev_timer *timer,
						 uint64_t num,
						 void *data)
{
	if (!data) {
		log_warn("No valid pointer passed to mouse_hide_timer_cb().");
		return;
	}

	struct kmscon_mouse_info* mouse = NULL;
	mouse = (struct kmscon_mouse_info*) data;
	mouse->hide = true;
}

void handle_clicks (struct kmscon_mouse_info* mouse, int button)
{
	if (!mouse)
		return;

	time_t seconds = mouse->last_down.tv_sec;
	suseconds_t useconds = mouse->last_down.tv_usec;
	gettimeofday(&mouse->last_up, NULL);

	// check for click
	if (seconds == mouse->last_up.tv_sec &&
		mouse->last_up.tv_usec - useconds <= 150000) {

		mouse->clicked[button] = true;

		// check for double-click
		time_t dbl_seconds = mouse->click.tv_sec;
		suseconds_t dbl_useconds = mouse->click.tv_usec;
		gettimeofday(&mouse->click, NULL);

		if (dbl_seconds == mouse->click.tv_sec &&
			mouse->click.tv_usec - dbl_useconds <= 300000) {

			mouse->dbl_clicked[button] = true;
		}
	}
}

void update_mouse_button_state(struct kmscon_mouse_info* mouse,
                               int button,
                               char button_mask,
                               char buffer)
{
	if (!mouse)
		return;

	int new_state = mouse->state[button];

	// released -> down
	if (mouse->state[button] == KMSCON_MOUSE_BUTTON_RELEASED &&
		(buffer & button_mask)) {
		new_state = KMSCON_MOUSE_BUTTON_DOWN;
		gettimeofday(&mouse->last_down, NULL);
	}

	// down -> pressed
	if (mouse->state[button] == KMSCON_MOUSE_BUTTON_DOWN &&
		(buffer & button_mask)) {
		new_state = KMSCON_MOUSE_BUTTON_PRESSED;
	}

	// pressed -> up
	if (mouse->state[button] == KMSCON_MOUSE_BUTTON_PRESSED &&
		!(buffer & button_mask)) {
		new_state = KMSCON_MOUSE_BUTTON_UP;
	}

	// up -> released
	if (mouse->state[button] == KMSCON_MOUSE_BUTTON_UP &&
		!(buffer & button_mask)) {
		new_state = KMSCON_MOUSE_BUTTON_RELEASED;
		handle_clicks (mouse, button);
	}

	// edge-case (no mouse motion): down -> released
	if (mouse->state[button] == KMSCON_MOUSE_BUTTON_DOWN &&
		!(buffer & button_mask)) {
		new_state = KMSCON_MOUSE_BUTTON_RELEASED;
		handle_clicks (mouse, button);
	}

	// edge-case (no mouse motion): up -> pressed
	if (mouse->state[button] == KMSCON_MOUSE_BUTTON_UP &&
		(buffer & button_mask)) {
		new_state = KMSCON_MOUSE_BUTTON_PRESSED;
	}

	mouse->state[button] = new_state;
}

void mouse_query_timer_cb(struct ev_timer *timer, uint64_t num, void *data)
{
	if (!data) {
		log_warn("No valid pointer passed to mouse_query_timer_cb().");
		return;
	}

	struct kmscon_mouse_info* mouse = (struct kmscon_mouse_info*) data;
	ssize_t size = 0;
	short relative_x = 0;
	short relative_y = 0;
	char buffer[BUFFER_SIZE];

	if (mouse_ready_to_read (mouse->device)) {
		size = read(mouse->device, buffer, sizeof (buffer));
		if (size != BUFFER_SIZE) {
			log_error("Error reading device event... buffer-size mismatch!");
			return;
		}
		relative_x = (short) buffer[1];
		relative_y = (short) buffer[2];
		mouse->x += .0025 * (float) relative_x/MOTION_SCALE;
		mouse->y += .0025 * (float) relative_y/MOTION_SCALE;

		// limit 'normalized' coordinates to the extents of the GL-viewport
		if (mouse->x < -1.f) mouse->x = -1.f;
		if (mouse->x > 1.f) mouse->x = 1.f;
		if (mouse->y < -1.f) mouse->y = -1.f;
		if (mouse->y > 1.f) mouse->y = 1.f;

		update_mouse_button_state (mouse,
								   KMSCON_MOUSE_BUTTON_LEFT,
								   BUTTON_MASK_LEFT,
								   buffer[0]);
		update_mouse_button_state (mouse,
								   KMSCON_MOUSE_BUTTON_MIDDLE,
								   BUTTON_MASK_MIDDLE,
								   buffer[0]);
		update_mouse_button_state (mouse,
								   KMSCON_MOUSE_BUTTON_RIGHT,
								   BUTTON_MASK_RIGHT,
								   buffer[0]);

		mouse->hide = false;

		// FIXME: Triggering the refresh like this works, but
		// it's not an elegant solution. Using an event would
		// be nicer, but I did not yet fully grasp the pile of
		// event-loops kmscon uses. There has to be a way to
		// trigger a refresh via the event-loops of uterm_video.
		kmscon_seat_refresh_display(mouse->seat, mouse->disp);
	}
}
