#include <linux/input-event-codes.h>
#include <linux/input.h>
#include "shl_hook.h"
#include "shl_llog.h"
#include "uterm_input.h"
#include "uterm_input_internal.h"


static void pointer_dev_send_move(struct uterm_input_dev *dev)
{
	struct uterm_input_pointer_event pev = {0};

	pev.event = UTERM_MOVED;
	pev.pointer_x = dev->pointer.x;
	pev.pointer_y = dev->pointer.y;

	shl_hook_call(dev->input->pointer_hook, dev->input, &pev);
}

static void pointer_dev_send_wheel(struct uterm_input_dev *dev, int32_t value)
{
	struct uterm_input_pointer_event pev = {0};

	pev.event = UTERM_WHEEL;
	pev.wheel = value;

	shl_hook_call(dev->input->pointer_hook, dev->input, &pev);
}

static void pointer_dev_send_button(struct uterm_input_dev *dev, uint8_t button, bool pressed)
{
	struct uterm_input_pointer_event pev = {0};

	pev.event = UTERM_BUTTON;
	pev.button = button;
	pev.pressed = pressed;

	shl_hook_call(dev->input->pointer_hook, dev->input, &pev);
}

void pointer_dev_sync(struct uterm_input_dev *dev)
{
	struct uterm_input_pointer_event pev = {0};

	pev.event = UTERM_SYNC;

	shl_hook_call(dev->input->pointer_hook, dev->input, &pev);
	dev->pointer.touchpaddown = false;
}

void pointer_dev_rel(struct uterm_input_dev *dev,
		      uint16_t code, int32_t value)
{
	switch (code) {
	case REL_X:
		dev->pointer.x += value;
		if (dev->pointer.x < 0)
			dev->pointer.x = 0;
		if (dev->pointer.x > dev->input->pointer_max_x)
			dev->pointer.x = dev->input->pointer_max_x;
		pointer_dev_send_move(dev);
		break;
	case REL_Y:
		dev->pointer.y += value;
		if (dev->pointer.y < 0)
			dev->pointer.y = 0;
		if (dev->pointer.y > dev->input->pointer_max_y)
			dev->pointer.y = dev->input->pointer_max_y;
		pointer_dev_send_move(dev);
		break;
	case REL_WHEEL:
		pointer_dev_send_wheel(dev, value);
		break;
	default:
		break;
	}
}

void pointer_dev_abs(struct uterm_input_dev *dev,
		     uint16_t code, int32_t value)
{
	switch (code) {
	case ABS_X:
		if (dev->pointer.kind == POINTER_TOUCHPAD) {
			if (dev->pointer.touchpaddown == true)
				dev->pointer.off_x = dev->pointer.x - value;

			dev->pointer.x = dev->pointer.off_x + value;
			if (dev->pointer.x < 0) {
				dev->pointer.x = 0;
				dev->pointer.off_x = -value;
			}
			if (dev->pointer.x > dev->input->pointer_max_x) {
				dev->pointer.x = dev->input->pointer_max_x;
				dev->pointer.off_x = dev->input->pointer_max_x - value;
			}
		} else if (dev->pointer.kind == POINTER_VMOUSE) {
			dev->pointer.x = ((value - dev->pointer.min_x) * dev->input->pointer_max_x) / (dev->pointer.max_x - dev->pointer.min_x);
		} else {
			return;
		}
		break;
	case ABS_Y:
		if (dev->pointer.kind == POINTER_TOUCHPAD) {
			if (dev->pointer.touchpaddown == true)
				dev->pointer.off_y = dev->pointer.y - value;

			dev->pointer.y = dev->pointer.off_y + value;
			if (dev->pointer.y < 0) {
				dev->pointer.y = 0;
				dev->pointer.off_y = -value;
			}
			if (dev->pointer.y > dev->input->pointer_max_y) {
				dev->pointer.y = dev->input->pointer_max_y;
				dev->pointer.off_y = dev->input->pointer_max_y - value;
			}
		} else if (dev->pointer.kind == POINTER_VMOUSE) {
			dev->pointer.y = ((value - dev->pointer.min_y) * dev->input->pointer_max_y) / (dev->pointer.max_y - dev->pointer.min_y);
		} else {
			return;
		}
		break;
	default:
		return;
	}
	pointer_dev_send_move(dev);
}

void pointer_dev_button(struct uterm_input_dev *dev,
			uint16_t code, int32_t value)
{
	bool pressed = (value == 1);

	switch (code) {
	case BTN_LEFT:
		pointer_dev_send_button(dev, 0, pressed);
		break;
	case BTN_RIGHT:
		pointer_dev_send_button(dev, 1, pressed);
		break;
	case BTN_TOOL_DOUBLETAP:
	case BTN_TOOL_TRIPLETAP:
	case BTN_MIDDLE:
		pointer_dev_send_button(dev, 2, pressed);
		break;
	case BTN_TOUCH:
		dev->pointer.touchpaddown = true;
		break;
	default:
		break;
	}
}

