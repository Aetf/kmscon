/*
 * kmscon - Pixman Text Renderer Backend
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

/*
 * Pixman based text renderer
 */

#include <errno.h>
#include <pixman.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "font_rotate.h"
#include "shl_hashtable.h"
#include "shl_log.h"
#include "shl_misc.h"
#include "text.h"
#include "uterm_video.h"

#define LOG_SUBSYSTEM "text_pixman"

struct tp_glyph {
	struct uterm_video_buffer vb;
	pixman_image_t *surf;
};

struct tp_pixman {
	pixman_image_t *white;
	struct shl_hashtable *glyphs;
	struct shl_hashtable *bold_glyphs;

	struct uterm_video_buffer buf[2];
	pixman_image_t *surf[2];
	unsigned int format[2];

	bool use_indirect;
	uint8_t *data[2];
	struct uterm_video_buffer vbuf;

	/* cache */
	unsigned int cur;
	unsigned int c_bpp;
	uint32_t *c_data;
	unsigned int c_stride;
};

static int tp_init(struct kmscon_text *txt)
{
	struct tp_pixman *tp;

	tp = malloc(sizeof(*tp));
	if (!tp)
		return -ENOMEM;

	txt->data = tp;
	return 0;
}

static void tp_destroy(struct kmscon_text *txt)
{
	struct tp_pixman *tp = txt->data;

	free(tp);
}

static void free_glyph(void *data)
{
	struct tp_glyph *glyph = data;

	pixman_image_unref(glyph->surf);
	free(glyph->vb.data);
	free(glyph);
}

static unsigned int format_u2p(unsigned int f)
{
	switch (f) {
	case UTERM_FORMAT_XRGB32:
		return PIXMAN_x8r8g8b8;
	case UTERM_FORMAT_RGB16:
		return PIXMAN_r5g6b5;
	case UTERM_FORMAT_GREY:
		return PIXMAN_a8;
	default:
		return 0;
	}
}

static int alloc_indirect(struct kmscon_text *txt,
			  unsigned int w, unsigned int h)
{
	struct tp_pixman *tp = txt->data;
	unsigned int s, i, format;

	log_info("using blitting engine");

	format = format_u2p(UTERM_FORMAT_XRGB32);
	s = w * 4;

	tp->data[0] = malloc(s * h);
	tp->data[1] = malloc(s * h);
	if (!tp->data[0] || !tp->data[1]) {
		log_error("cannot allocate memory for render-buffer");
		goto err_free;
	}

	for (i = 0; i < 2; ++i) {
		tp->format[i] = format;
		tp->surf[i] = pixman_image_create_bits(format, w, h,
						       (void*)tp->data[i], s);
		if (!tp->surf[i]) {
			log_error("cannot create pixman surfaces");
			goto err_pixman;
		}
	}

	tp->vbuf.width = w;
	tp->vbuf.height = h;
	tp->vbuf.stride = s;
	tp->vbuf.format = UTERM_FORMAT_XRGB32;
	tp->use_indirect = true;
	return 0;

err_pixman:
	if (tp->surf[1])
		pixman_image_unref(tp->surf[1]);
	tp->surf[1] = NULL;
	if (tp->surf[0])
		pixman_image_unref(tp->surf[0]);
	tp->surf[0] = NULL;
err_free:
	free(tp->data[1]);
	free(tp->data[0]);
	tp->data[1] = NULL;
	tp->data[0] = NULL;
	return -ENOMEM;
}

static int tp_set(struct kmscon_text *txt)
{
	struct tp_pixman *tp = txt->data;
	int ret;
	unsigned int w, h;
	struct uterm_mode *m;
	pixman_color_t white;

	memset(tp, 0, sizeof(*tp));
	m = uterm_display_get_current(txt->disp);
	w = uterm_mode_get_width(m);
	h = uterm_mode_get_height(m);

	white.red = 0xffff;
	white.green = 0xffff;
	white.blue = 0xffff;
	white.red = 0xffff;

	tp->white = pixman_image_create_solid_fill(&white);
	if (!tp->white) {
		log_error("cannot create pixman solid color buffer");
		return -ENOMEM;
	}
	ret = kmscon_rotate_create_tables(&tp->glyphs, &tp->bold_glyphs, free_glyph);
	if (ret)
		goto err_white;

	/*
	 * TODO: It is actually faster to use a local shadow buffer and then
	 * blit all data to the framebuffer afterwards. Reads seem to be
	 * horribly slow on some mmap'ed framebuffers. However, that's not true
	 * for all so we actually don't know which to use here.
	 */
	ret = uterm_display_get_buffers(txt->disp, tp->buf,
					UTERM_FORMAT_XRGB32);
	if (ret) {
		log_warning("cannot get buffers for display %p",
			    txt->disp);
		ret = alloc_indirect(txt, w, h);
		if (ret)
			goto err_glyph_table;
	} else {
		tp->format[0] = format_u2p(tp->buf[0].format);
		tp->surf[0] = pixman_image_create_bits_no_clear(tp->format[0],
					tp->buf[0].width, tp->buf[0].height,
					(void*)tp->buf[0].data,
					tp->buf[0].stride);
		tp->format[1] = format_u2p(tp->buf[1].format);
		tp->surf[1] = pixman_image_create_bits_no_clear(tp->format[1],
					tp->buf[1].width, tp->buf[1].height,
					(void*)tp->buf[1].data,
					tp->buf[1].stride);
		if (!tp->surf[0] || !tp->surf[1]) {
			log_error("cannot create pixman surfaces");
			goto err_ctx;
		}
	}
	if (txt->orientation == OR_NORMAL || txt->orientation == OR_UPSIDE_DOWN) {
		txt->cols = w / txt->font->attr.width;
		txt->rows = h / txt->font->attr.height;
	} else {
		txt->cols = h / txt->font->attr.width;
		txt->rows = w / txt->font->attr.height;
	}

	return 0;

err_ctx:
	if (tp->surf[1])
		pixman_image_unref(tp->surf[1]);
	if (tp->surf[0])
		pixman_image_unref(tp->surf[0]);
	free(tp->data[1]);
	free(tp->data[0]);
err_glyph_table:
	kmscon_rotate_free_tables(tp->glyphs, tp->bold_glyphs);
err_white:
	pixman_image_unref(tp->white);
	return ret;
}

static void tp_unset(struct kmscon_text *txt)
{
	struct tp_pixman *tp = txt->data;

	pixman_image_unref(tp->surf[1]);
	pixman_image_unref(tp->surf[0]);
	free(tp->data[1]);
	free(tp->data[0]);
	kmscon_rotate_free_tables(tp->glyphs, tp->bold_glyphs);
	pixman_image_unref(tp->white);
}

static int find_glyph(struct kmscon_text *txt, struct tp_glyph **out,
		      uint64_t id, const uint32_t *ch, size_t len, const struct tsm_screen_attr *attr)
{
	struct tp_pixman *tp = txt->data;
	const struct kmscon_glyph *glyph;
	struct tp_glyph *tp_glyph;
	struct shl_hashtable *gtable;
	struct kmscon_font *font;
	int ret;
	bool res;

	if (attr->bold) {
		gtable = tp->bold_glyphs;
		font = txt->bold_font;
	} else {
		gtable = tp->glyphs;
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

	res = shl_hashtable_find(gtable, (void**)&tp_glyph, id);
	if (res) {
		*out = tp_glyph;
		return 0;
	}

	tp_glyph = malloc(sizeof(*tp_glyph));
	if (!tp_glyph)
		return -ENOMEM;
	memset(tp_glyph, 0, sizeof(*tp_glyph));

	if (!len)
		ret = kmscon_font_render_empty(font, &glyph);
	else
		ret = kmscon_font_render(font, id, ch, len, &glyph);

	if (ret) {
		ret = kmscon_font_render_inval(font, &glyph);
		if (ret)
			goto err_free;
	}

	ret = kmscon_rotate_glyph(&tp_glyph->vb, glyph, txt->orientation, 4);
	if (ret)
		goto err_free;


	tp_glyph->surf = pixman_image_create_bits_no_clear(PIXMAN_a8,
							   tp_glyph->vb.width,
							   tp_glyph->vb.height,
							   (void*) tp_glyph->vb.data,
							   tp_glyph->vb.stride);

	if (!tp_glyph->surf) {
		log_error("cannot create pixman-glyph: %d %p %d %d %d",
			  ret, tp_glyph->vb.data, tp_glyph->vb.width, tp_glyph->vb.height, tp_glyph->vb.stride);
		ret = -EFAULT;
		goto err_free_vb;
	}

	ret = shl_hashtable_insert(gtable, id, tp_glyph);
	if (ret)
		goto err_pixman;

	*out = tp_glyph;
	return 0;

err_pixman:
	pixman_image_unref(tp_glyph->surf);

err_free_vb:
	free(tp_glyph->vb.data);
err_free:
	free(tp_glyph);
	return ret;
}

static int tp_rotate(struct kmscon_text *txt, enum Orientation orientation)
{
	struct tp_pixman *tp = txt->data;
	unsigned int w, h;
	struct uterm_mode *m;

	m = uterm_display_get_current(txt->disp);
	w = uterm_mode_get_width(m);
	h = uterm_mode_get_height(m);

	txt->orientation = orientation;

	if (txt->orientation == OR_NORMAL || txt->orientation == OR_UPSIDE_DOWN) {
		txt->cols = w / txt->font->attr.width;
		txt->rows = h / txt->font->attr.height;
	} else {
		txt->cols = h / txt->font->attr.width;
		txt->rows = w / txt->font->attr.height;
	}

	// Free glyph cache, as the glyph are rotated in the cache.
	kmscon_rotate_free_tables(tp->glyphs, tp->bold_glyphs);

	return kmscon_rotate_create_tables(&tp->glyphs, &tp->bold_glyphs, free_glyph);
}

static int tp_prepare(struct kmscon_text *txt)
{
	struct tp_pixman *tp = txt->data;
	int ret;
	pixman_image_t *img;

	ret = uterm_display_use(txt->disp, NULL);
	if (ret < 0) {
		log_error("cannot use display %p", txt->disp);
		return ret;
	}

	tp->cur = ret;
	img = tp->surf[tp->cur];
	tp->c_bpp = PIXMAN_FORMAT_BPP(tp->format[tp->cur]);
	tp->c_data = pixman_image_get_data(img);
	tp->c_stride = pixman_image_get_stride(img);

	return 0;
}

static int tp_draw(struct kmscon_text *txt,
		   uint64_t id, const uint32_t *ch, size_t len,
		   unsigned int width,
		   unsigned int posx, unsigned int posy,
		   const struct tsm_screen_attr *attr)
{
	struct tp_pixman *tp = txt->data;
	struct uterm_mode *mode;
	struct tp_glyph *glyph;
	int ret;
	unsigned int x, y, w, h, sw, sh, cwidth;
	uint32_t bc;
	pixman_color_t fc;
	pixman_image_t *col;

	if (!width)
		return 0;

	mode = uterm_display_get_current(txt->disp);
	if (!mode)
		return -EINVAL;
	sw = uterm_mode_get_width(mode);
	sh = uterm_mode_get_height(mode);

	ret = find_glyph(txt, &glyph, id, ch, len, attr);
	if (ret)
		return ret;

	if (attr->inverse) {
		bc = (attr->fr << 16) | (attr->fg << 8) | (attr->fb);
		fc.red = attr->br << 8;
		fc.green = attr->bg << 8;
		fc.blue = attr->bb << 8;
		fc.alpha = 0xffff;
	} else {
		bc = (attr->br << 16) | (attr->bg << 8) | (attr->bb);
		fc.red = attr->fr << 8;
		fc.green = attr->fg << 8;
		fc.blue = attr->fb << 8;
		fc.alpha = 0xffff;
	}

	/* TODO: We _really_ should fix pixman to allow something like
	 * pixman_image_set_solid_fill(img, &fc) to avoid allocating a pixman
	 * image for each glyph here.
	 * libc malloc() is pretty fast, but this still costs us a lot of
	 * rendering performance. */
	if (fc.red == 0xff00 && fc.green == 0xff00 && fc.blue == 0xff00) {
		col = tp->white;
		pixman_image_ref(col);
	} else {
		col = pixman_image_create_solid_fill(&fc);
		if (!col) {
			log_error("cannot create pixman color image");
			return -ENOMEM;
		}
	}

	w = glyph->vb.width;
	h = glyph->vb.height;

	switch (txt->orientation) {
	default:
	case OR_NORMAL:
		x = posx * FONT_WIDTH(txt);
		y = posy * FONT_HEIGHT(txt);
		break;
	case OR_UPSIDE_DOWN:
		cwidth = w / FONT_WIDTH(txt);
		x = sw - (posx + cwidth) * FONT_WIDTH(txt);
		y = sh - (posy + 1) * FONT_HEIGHT(txt);
		break;
	case OR_RIGHT:
		x = sw - (posy + 1) * FONT_HEIGHT(txt);
		y = posx * FONT_WIDTH(txt);
		break;
	case OR_LEFT:
		cwidth = h / FONT_WIDTH(txt);
		x = posy * FONT_HEIGHT(txt);
		y = sh - (posx + cwidth) * FONT_WIDTH(txt);
		break;
	}

	if (!bc) {
		pixman_image_composite(PIXMAN_OP_SRC,
				       col,
				       glyph->surf,
				       tp->surf[tp->cur],
				       0, 0, 0, 0,
				       x, y, w, h);
	} else {
		pixman_fill(tp->c_data, tp->c_stride / 4, tp->c_bpp,
			    x, y, w, h, bc);

		pixman_image_composite(PIXMAN_OP_OVER,
				       col,
				       glyph->surf,
				       tp->surf[tp->cur],
				       0, 0, 0, 0,
				       x, y, w, h);
	}

	pixman_image_unref(col);

	return 0;
}

static int tp_draw_pointer(struct kmscon_text *txt,
			   unsigned int pointer_x, unsigned int pointer_y,
			   const struct tsm_screen_attr *attr)
{
	struct tp_pixman *tp = txt->data;
	struct uterm_mode *mode;
	struct tp_glyph *glyph;
	uint32_t ch = 'I';
	uint64_t id = ch;
	int ret;
	unsigned int x, y, w, h, sw, sh;
	unsigned int m_x, m_y;
	uint32_t bc;
	pixman_color_t fc;
	pixman_image_t *col;

	mode = uterm_display_get_current(txt->disp);
	if (!mode)
		return -EINVAL;
	sw = uterm_mode_get_width(mode);
	sh = uterm_mode_get_height(mode);

	ret = find_glyph(txt, &glyph, id, &ch, 1, attr);
	if (ret)
		return ret;

	bc = (attr->br << 16) | (attr->bg << 8) | (attr->bb);
	fc.red = attr->fr << 8;
	fc.green = attr->fg << 8;
	fc.blue = attr->fb << 8;
	fc.alpha = 0xffff;

	/* TODO: We _really_ should fix pixman to allow something like
	 * pixman_image_set_solid_fill(img, &fc) to avoid allocating a pixman
	 * image for each glyph here.
	 * libc malloc() is pretty fast, but this still costs us a lot of
	 * rendering performance. */
	if (attr->fr == 0xff && attr->fg == 0xff && attr->fb == 0xff) {
		col = tp->white;
		pixman_image_ref(col);
	} else {
		col = pixman_image_create_solid_fill(&fc);
		if (!col) {
			log_error("cannot create pixman color image");
			return -ENOMEM;
		}
	}

	if (txt->orientation == OR_NORMAL || txt->orientation == OR_UPSIDE_DOWN) {
		w = FONT_WIDTH(txt);
		h = FONT_HEIGHT(txt);
	} else {
		w = FONT_HEIGHT(txt);
		h = FONT_WIDTH(txt);
	}

	m_x = SHL_DIV_ROUND_UP(w, 2);
	m_y = SHL_DIV_ROUND_UP(h, 2);

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

	if (!bc) {
		pixman_image_composite(PIXMAN_OP_SRC,
				       col,
				       glyph->surf,
				       tp->surf[tp->cur],
				       0, 0, 0, 0,
				       x, y, w, h);
	} else {
		pixman_fill(tp->c_data, tp->c_stride / 4, tp->c_bpp,
			    x, y, w, h, bc);

		pixman_image_composite(PIXMAN_OP_OVER,
				       col,
				       glyph->surf,
				       tp->surf[tp->cur],
				       0, 0, 0, 0,
				       x, y, w, h);
	}

	pixman_image_unref(col);

	return 0;
}

static int tp_render(struct kmscon_text *txt)
{
	struct tp_pixman *tp = txt->data;
	int ret;

	if (!tp->use_indirect)
		return 0;

	tp->vbuf.data = tp->data[tp->cur];
	ret = uterm_display_blit(txt->disp, &tp->vbuf, 0, 0);
	if (ret) {
		log_error("cannot blit back-buffer to display: %d", ret);
		return ret;
	}

	return 0;
}

struct kmscon_text_ops kmscon_text_pixman_ops = {
	.name = "pixman",
	.owner = NULL,
	.init = tp_init,
	.destroy = tp_destroy,
	.set = tp_set,
	.unset = tp_unset,
	.rotate = tp_rotate,
	.prepare = tp_prepare,
	.draw = tp_draw,
	.draw_pointer = tp_draw_pointer,
	.render = tp_render,
	.abort = NULL,
};
