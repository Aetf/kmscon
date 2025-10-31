/*
 * kmscon - Bit-Blitting Bulk Text Renderer Backend
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
 * SECTION:text_bbulk.c
 * @short_description: Bit-Blitting Bulk Text Renderer Backend
 * @include: text.h
 *
 * Similar to the bblit renderer but assembles an array of blit-requests and
 * pushes all of them at once to the video device.
 */

#include <asm-generic/errno.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "font_rotate.h"
#include "shl_hashtable.h"
#include "shl_log.h"
#include "text.h"
#include "uterm_video.h"

#define LOG_SUBSYSTEM "text_bbulk"

struct bbulk {
	struct uterm_video_blend_req *reqs;
	struct shl_hashtable *glyphs;
	struct shl_hashtable *bold_glyphs;
};

static int bbulk_init(struct kmscon_text *txt)
{
	struct bbulk *bb;

	bb = malloc(sizeof(*bb));
	if (!bb)
		return -ENOMEM;

	txt->data = bb;
	return 0;
}

static void bbulk_destroy(struct kmscon_text *txt)
{
	struct bbulk *bb = txt->data;

	free(bb);
}

static void free_glyph(void *data)
{
	struct uterm_video_buffer *bb_glyph = data;

	free(bb_glyph->data);
	free(bb_glyph);
}

static int bbulk_set(struct kmscon_text *txt)
{
	struct bbulk *bb = txt->data;
	unsigned int sw, sh, i, j;
	struct uterm_video_blend_req *req;
	struct uterm_mode *mode;

	memset(bb, 0, sizeof(*bb));

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
	bb->reqs = malloc(sizeof(*bb->reqs) * txt->cols * txt->rows);
	if (!bb->reqs)
		return -ENOMEM;
	memset(bb->reqs, 0, sizeof(*bb->reqs) * txt->cols * txt->rows);

	for (i = 0; i < txt->rows; ++i) {
		for (j = 0; j < txt->cols; ++j) {
			req = &bb->reqs[i * txt->cols + j];
			switch (txt->orientation) {
			default:
			case OR_NORMAL:
				req->x = j * FONT_WIDTH(txt);
				req->y = i * FONT_HEIGHT(txt);
				break;
			case OR_UPSIDE_DOWN:
				req->x = sw - (j + 1) * FONT_WIDTH(txt);
				req->y = sh - (i + 1) * FONT_HEIGHT(txt);
				break;
			case OR_RIGHT:
				req->x = sw - (i + 1) * FONT_HEIGHT(txt);
				req->y = j * FONT_WIDTH(txt);
				break;
			case OR_LEFT:
				req->x = i * FONT_HEIGHT(txt);
				req->y = sh - (j + 1) * FONT_WIDTH(txt);
				break;
			}

		}
	}
	if (kmscon_rotate_create_tables(&bb->glyphs, &bb->bold_glyphs, free_glyph))
		goto free_reqs;
	return 0;

free_reqs:
	free(bb->reqs);
	return -ENOMEM;
}

static void bbulk_unset(struct kmscon_text *txt)
{
	struct bbulk *bb = txt->data;

	kmscon_rotate_free_tables(bb->glyphs, bb->bold_glyphs);
	free(bb->reqs);
	bb->reqs = NULL;
}

static int bbulk_rotate(struct kmscon_text *txt, enum Orientation orientation)
{
	bbulk_unset(txt);
	txt->orientation = orientation;
	return bbulk_set(txt);
}

static int find_glyph(struct kmscon_text *txt, struct uterm_video_buffer **out,
		      uint64_t id, const uint32_t *ch, size_t len, const struct tsm_screen_attr *attr)
{
	struct bbulk *bb = txt->data;
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

static int bbulk_draw(struct kmscon_text *txt,
		      uint64_t id, const uint32_t *ch, size_t len,
		      unsigned int width,
		      unsigned int posx, unsigned int posy,
		      const struct tsm_screen_attr *attr)
{
	struct bbulk *bb = txt->data;
	struct uterm_video_buffer *bb_glyph;
	struct uterm_video_blend_req *req;
	int ret;

	if (!width) {
		bb->reqs[posy * txt->cols + posx].buf = NULL;
		return 0;
	}
	ret = find_glyph(txt, &bb_glyph, id, ch, len, attr);
	if (ret)
		return ret;

	req = &bb->reqs[posy * txt->cols + posx];

	/*
	 * In case of left or upside down orientation, we need to draw to the
	 * next cell, as the glyph is already rotated, so start on the next cell
	 * and end on this cell
	 */
	if (txt->orientation == OR_LEFT || txt->orientation == OR_UPSIDE_DOWN) {
		if (txt->overflow_next && posx + 1 < txt->cols) {
			req = &bb->reqs[posy * txt->cols + posx + 1];
		}
	}

	req->buf = bb_glyph;
	if (attr->inverse) {
		req->fr = attr->br;
		req->fg = attr->bg;
		req->fb = attr->bb;
		req->br = attr->fr;
		req->bg = attr->fg;
		req->bb = attr->fb;
	} else {
		req->fr = attr->fr;
		req->fg = attr->fg;
		req->fb = attr->fb;
		req->br = attr->br;
		req->bg = attr->bg;
		req->bb = attr->bb;
	}

	return 0;
}

static int bbulk_render(struct kmscon_text *txt)
{
	struct bbulk *bb = txt->data;
	int ret;

	ret = uterm_display_fake_blendv(txt->disp, bb->reqs,
					 txt->cols * txt->rows);
	return ret;
}

static int bbulk_prepare(struct kmscon_text *txt)
{
	struct bbulk *bb = txt->data;
	int i;

	// Clear previous requests
	for (i = 0; i < txt->rows * txt->cols; ++i)
		bb->reqs[i].buf = NULL;

	return 0;
}

struct kmscon_text_ops kmscon_text_bbulk_ops = {
	.name = "bbulk",
	.owner = NULL,
	.init = bbulk_init,
	.destroy = bbulk_destroy,
	.set = bbulk_set,
	.unset = bbulk_unset,
	.rotate = bbulk_rotate,
	.prepare = bbulk_prepare,
	.draw = bbulk_draw,
	.render = bbulk_render,
	.abort = NULL,
};
