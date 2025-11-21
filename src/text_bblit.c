/*
 * kmscon - Bit-Blitting Text Renderer Backend
 *
 * Copyright (c) 2012-2013 David Herrmann <dh.herrmann@googlemail.com>
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

/**
 * SECTION:text_bblit.c
 * @short_description: Bit-Blitting Text Renderer Backend
 * @include: text.h
 *
 * The bit-blitting renderer requires framebuffer access to the output device
 * and simply blits the glyphs into the buffer.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "font_rotate.h"
#include "shl_log.h"
#include "shl_misc.h"
#include "text.h"
#include "uterm_video.h"

#define LOG_SUBSYSTEM "text_bblit"

struct bblit {
	struct shl_hashtable *glyphs;
	struct shl_hashtable *bold_glyphs;
};

static int bblit_init(struct kmscon_text *txt)
{
	struct bblit *bb;

	bb = malloc(sizeof(*bb));
	if (!bb)
		return -ENOMEM;

	txt->data = bb;
	return 0;
}

static void bblit_destroy(struct kmscon_text *txt)
{
	struct bblit *bb = txt->data;

	free(bb);
}

static void free_glyph(void *data)
{
	struct uterm_video_buffer *bb_glyph = data;

	free(bb_glyph->data);
	free(bb_glyph);
}

static int bblit_set(struct kmscon_text *txt)
{
	struct bblit *bb = txt->data;
	unsigned int sw, sh;
	struct uterm_mode *mode;

	mode = uterm_display_get_current(txt->disp);
	if (!mode)
		return -EINVAL;
	sw = uterm_mode_get_width(mode);
	sh = uterm_mode_get_height(mode);

	if (txt->orientation == OR_NORMAL || txt->orientation == OR_UPSIDE_DOWN) {
		txt->cols = sw / FONT_WIDTH(txt);
		txt->rows = sh / FONT_HEIGHT(txt);
	} else {
		txt->rows = sw / FONT_HEIGHT(txt);
		txt->cols = sh / FONT_WIDTH(txt);
	}

	return kmscon_rotate_create_tables(&bb->glyphs, &bb->bold_glyphs, free_glyph);
}

static void bblit_unset(struct kmscon_text *txt)
{
	struct bblit *bb = txt->data;

	kmscon_rotate_free_tables(bb->glyphs, bb->bold_glyphs);
}

static int bblit_rotate(struct kmscon_text *txt, enum Orientation orientation)
{
	bblit_unset(txt);
	txt->orientation = orientation;
	return bblit_set(txt);
}

static int find_glyph(struct kmscon_text *txt, struct uterm_video_buffer **out,
		      uint64_t id, const uint32_t *ch, size_t len, const struct tsm_screen_attr *attr)
{
	struct bblit *bb = txt->data;
	struct uterm_video_buffer *bb_glyph;
	const struct kmscon_glyph *glyph;
	struct shl_hashtable *gtable;
	struct kmscon_font *font;
	int ret;
	bool res;

	if (attr->bold) {
		gtable = bb->bold_glyphs;
		font = txt->bold_font;
	} else {
		gtable = bb->glyphs;
		font = txt->font;
	}

	if (attr->underline)
		font->attr.underline = true;
	else
		font->attr.underline = false;

	if (attr->italic)
		font->attr.italic = true;
	else
		font->attr.italic = false;

	res = shl_hashtable_find(gtable, (void**)&bb_glyph, id);
	if (res) {
		*out = bb_glyph;
		return 0;
	}

	bb_glyph = malloc(sizeof(*bb_glyph));
	if (!bb_glyph)
		return -ENOMEM;
	memset(bb_glyph, 0, sizeof(*bb_glyph));

	if (!len)
		ret = kmscon_font_render_empty(font, &glyph);
	else
		ret = kmscon_font_render(font, id, ch, len, &glyph);

	if (ret) {
		ret = kmscon_font_render_inval(font, &glyph);
		if (ret)
			goto err_free;
	}

	ret = kmscon_rotate_glyph( bb_glyph, glyph, txt->orientation, 1);
	if (ret)
		goto err_free;

	ret = shl_hashtable_insert(gtable, id, bb_glyph);
	if (ret)
		goto err_free_vb;

	*out = bb_glyph;
	return 0;

err_free_vb:
	free(bb_glyph->data);
err_free:
	free(bb_glyph);
	return ret;
}


static int bblit_draw(struct kmscon_text *txt,
		      uint64_t id, const uint32_t *ch, size_t len,
		      unsigned int width,
		      unsigned int posx, unsigned int posy,
		      const struct tsm_screen_attr *attr)
{
	struct uterm_video_buffer *bb_glyph;
	struct uterm_mode *mode;
	unsigned sw, sh, x, y, cwidth;
	int ret;

	if (!width)
		return 0;

	mode = uterm_display_get_current(txt->disp);
	if (!mode)
		return -EINVAL;
	sw = uterm_mode_get_width(mode);
	sh = uterm_mode_get_height(mode);

	ret = find_glyph(txt, &bb_glyph, id, ch, len, attr);
	if (ret)
		return ret;

	switch (txt->orientation) {
	default:
	case OR_NORMAL:
		x = posx * FONT_WIDTH(txt);
		y = posy * FONT_HEIGHT(txt);
		break;
	case OR_UPSIDE_DOWN:
		cwidth = bb_glyph->width / FONT_WIDTH(txt);
		x = sw - (posx + cwidth) * FONT_WIDTH(txt);
		y = sh - (posy + 1) * FONT_HEIGHT(txt);
		break;
	case OR_RIGHT:
		x = sw - (posy + 1) * FONT_HEIGHT(txt);
		y = posx * FONT_WIDTH(txt);
		break;
	case OR_LEFT:
		cwidth = bb_glyph->height / FONT_WIDTH(txt);
		x = posy * FONT_HEIGHT(txt);
		y = sh - (posx + cwidth) * FONT_WIDTH(txt);
		break;
	}

	/* draw glyph */
	if (attr->inverse) {
		ret = uterm_display_fake_blend(txt->disp, bb_glyph, x, y,
					       attr->br, attr->bg, attr->bb,
					       attr->fr, attr->fg, attr->fb);
	} else {
		ret = uterm_display_fake_blend(txt->disp, bb_glyph, x, y,
					       attr->fr, attr->fg, attr->fb,
					       attr->br, attr->bg, attr->bb);
	}

	return ret;
}

static int bblit_draw_pointer(struct kmscon_text *txt,
			      unsigned int pointer_x, unsigned int pointer_y,
			      const struct tsm_screen_attr *attr)
{
	struct uterm_video_buffer *bb_glyph;
	struct uterm_mode *mode;
	uint32_t ch = 'I';
	uint64_t id = ch;
	unsigned int sw, sh;
	unsigned int m_x, m_y, x, y;
	int ret;

	mode = uterm_display_get_current(txt->disp);
	if (!mode)
		return -EINVAL;
	sw = uterm_mode_get_width(mode);
	sh = uterm_mode_get_height(mode);

	if (txt->orientation == OR_NORMAL || txt->orientation == OR_UPSIDE_DOWN) {
		m_x = SHL_DIV_ROUND_UP(FONT_WIDTH(txt), 2);
		m_y = SHL_DIV_ROUND_UP(FONT_HEIGHT(txt), 2);
	} else {
		m_x = SHL_DIV_ROUND_UP(FONT_HEIGHT(txt), 2);
		m_y = SHL_DIV_ROUND_UP(FONT_WIDTH(txt), 2);
	}

	ret = find_glyph(txt, &bb_glyph, id, &ch, 1, attr);
	if (ret)
		return ret;

	switch (txt->orientation) {
	default:
	case OR_NORMAL:
		x = pointer_x;
		y = pointer_y;
		break;
	case OR_UPSIDE_DOWN:
		x = sw - pointer_x;
		y = sh - pointer_y;
		break;
	case OR_RIGHT:
		x = sw - pointer_y;
		y = pointer_x;
		break;
	case OR_LEFT:
		x = pointer_y;
		y = sh - pointer_x;
		break;
	}
	if (x < m_x)
		x = m_x;
	if (x + m_x > sw)
		x = sw - m_x;
	if (y < m_y)
		y = m_y;
	if (y + m_y > sh)
		y = sh - m_y;
	x -= m_x;
	y -= m_y;

	/* draw glyph */
	ret = uterm_display_fake_blend(txt->disp, bb_glyph, x, y,
					       attr->fr, attr->fg, attr->fb,
					       attr->br, attr->bg, attr->bb);
	return ret;
}

struct kmscon_text_ops kmscon_text_bblit_ops = {
	.name = "bblit",
	.owner = NULL,
	.init = bblit_init,
	.destroy = bblit_destroy,
	.set = bblit_set,
	.unset = bblit_unset,
	.rotate = bblit_rotate,
	.prepare = NULL,
	.draw = bblit_draw,
	.draw_pointer = bblit_draw_pointer,
	.render = NULL,
	.abort = NULL,
};
