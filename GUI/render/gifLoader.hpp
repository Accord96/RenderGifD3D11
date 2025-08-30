#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>

typedef struct
{
	int16_t prefix;
	byte first;
	byte suffix;
} stbi__gif_lzw;

typedef struct
{
	int w, h;
	byte* out;
	byte* background;
	byte* history;
	int flags, bgindex, ratio, transparent, eflags;
	byte  pal[256][4];
	byte lpal[256][4];
	stbi__gif_lzw codes[8192];
	byte* color_table;
	int parse, step;
	int lflags;
	int start_x, start_y;
	int max_x, max_y;
	int cur_x, cur_y;
	int line_size;
	int delay;
} stbi__gif;

#define GIFIMP static

GIFIMP int stbi__addsizes_valid(int a, int b)
{
	if (b < 0) return 0; return a <= INT_MAX - b;
}

GIFIMP int stbi__mul2sizes_valid(int a, int b)
{
	if (a < 0 || b < 0)
		return 0;
	if (b == 0)
		return 1;
	return a <= INT_MAX / b;
}

GIFIMP int stbi__mul3sizes_valid(int a, int b, int c)
{
	return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a * b, c);
}

GIFIMP int stbi__mad3sizes_valid(int a, int b, int c, int add)
{
	return stbi__mul3sizes_valid(a, b, c) && stbi__addsizes_valid(a * b * c, add);
}

GIFIMP void* stbi__malloc(size_t size)
{
	return malloc(size);
}

typedef struct
{
	byte const* img_buffer, * img_buffer_end;
	const byte* img_buffer_original, * img_buffer_original_end;
	int read_from_callbacks;
	int img_n, img_out_n;
	int img_x, img_y;
} stbi__context;

GIFIMP void stbi__start_mem(stbi__context* s, byte const* buffer, int len)
{
	s->img_buffer = buffer;
	s->img_buffer_end = buffer + len;
	s->img_buffer_original = buffer;
	s->img_buffer_original_end = buffer + len;
	s->read_from_callbacks = 0;
	s->img_x = s->img_y = 0;
	s->img_n = s->img_out_n = 0;
}

GIFIMP int stbi__at_eof(stbi__context* s)
{
	return s->img_buffer >= s->img_buffer_end;
}

GIFIMP unsigned char stbi__get8(stbi__context* s)
{
	if (stbi__at_eof(s))
		return 0;
	return *s->img_buffer++;
}

GIFIMP int stbi__get16le(stbi__context* s)
{
	int z = stbi__get8(s);
	return z + (stbi__get8(s) << 8);
}

GIFIMP int stbi__get32le(stbi__context* s)
{
	int z = stbi__get16le(s);
	return z + (stbi__get16le(s) << 16);
}

GIFIMP void stbi__skip(stbi__context* s, int n)
{
	s->img_buffer += n;
	if (s->img_buffer > s->img_buffer_end)
		s->img_buffer = s->img_buffer_end;
}

GIFIMP int stbi__getn(stbi__context* s, byte* buf, int n)
{
	if (s->img_buffer + n > s->img_buffer_end) return 0;
	memcpy(buf, s->img_buffer, n);
	s->img_buffer += n;
	return 1;
}

GIFIMP void stbi__rewind(stbi__context* s)
{
	s->img_buffer = s->img_buffer_original;
	s->img_buffer_end = s->img_buffer_original_end;
}

GIFIMP byte* stbi__convert_format(byte* data, int img_n, int req_comp, int x, int y)
{
	if (!req_comp || req_comp == img_n)
		return data;

	if (img_n == 4 && (req_comp == 3 || req_comp == 1 || req_comp == 2))
	{
		int i, j;
		int n = x * y;
		byte* out = (byte*)malloc((size_t)n * req_comp);
		if (!out) { free(data); return (byte*)NULL; }
		const byte* src = data;
		byte* dst = out;
		if (req_comp == 3) {
			for (i = 0; i < n; ++i) { *dst++ = src[0]; *dst++ = src[1]; *dst++ = src[2]; src += 4; }
		}
		else if (req_comp == 2) {
			for (i = 0; i < n; ++i) { *dst++ = (byte)((src[0] * 77 + src[1] * 150 + src[2] * 29) >> 8); *dst++ = src[3]; src += 4; }
		}
		else {
			for (i = 0; i < n; ++i) { *dst++ = (byte)((src[0] * 77 + src[1] * 150 + src[2] * 29) >> 8); src += 4; }
		}
		free(data);
		return out;
	}

	free(data);
	return (byte*)NULL;
}

GIFIMP void stbi__vertical_flip_slices(byte* pixels, int w, int h, int z, int comp)
{
	int layer, row;
	size_t stride = (size_t)w * comp;
	for (layer = 0; layer < z; ++layer)
	{
		byte* base = pixels + (size_t)layer * stride * h;
		for (row = 0; row < h / 2; ++row)
		{
			byte* a = base + (size_t)row * stride;
			byte* b = base + (size_t)(h - 1 - row) * stride;
			for (size_t i = 0; i < stride; ++i)
			{
				byte t = a[i]; a[i] = b[i]; b[i] = t;
			}
		}
	}
}

GIFIMP int stbi__gif_test_raw(stbi__context* s)
{
	int sz;
	if (stbi__get8(s) != 'G' || stbi__get8(s) != 'I' || stbi__get8(s) != 'F' || stbi__get8(s) != '8') return 0;
	sz = stbi__get8(s);
	if (sz != '9' && sz != '7') return 0;
	if (stbi__get8(s) != 'a') return 0;
	return 1;
}

GIFIMP int stbi__gif_test(stbi__context* s)
{
	int r = stbi__gif_test_raw(s);
	stbi__rewind(s);
	return r;
}

GIFIMP void stbi__gif_parse_colortable(stbi__context* s, byte pal[256][4], int num_entries, int transp)
{
	int i;
	for (i = 0; i < num_entries; ++i) {
		pal[i][2] = stbi__get8(s);
		pal[i][1] = stbi__get8(s);
		pal[i][0] = stbi__get8(s);
		pal[i][3] = transp == i ? 0 : 255;
	}
}

GIFIMP int stbi__gif_header(stbi__context* s, stbi__gif* g, int* comp, int is_info)
{
	byte version;
	if (stbi__get8(s) != 'G' || stbi__get8(s) != 'I' || stbi__get8(s) != 'F' || stbi__get8(s) != '8')
		return NULL;

	version = stbi__get8(s);
	if (version != '7' && version != '9')    return NULL;
	if (stbi__get8(s) != 'a')                return NULL;

	g->w = stbi__get16le(s);
	g->h = stbi__get16le(s);
	g->flags = stbi__get8(s);
	g->bgindex = stbi__get8(s);
	g->ratio = stbi__get8(s);
	g->transparent = -1;

	const auto STBI_MAX_DIMENSIONS = (1 << 24);

	if (g->w > STBI_MAX_DIMENSIONS) return NULL;
	if (g->h > STBI_MAX_DIMENSIONS) return NULL;

	if (comp != 0) *comp = 4;

	if (is_info) return 1;

	if (g->flags & 0x80)
		stbi__gif_parse_colortable(s, g->pal, 2 << (g->flags & 7), -1);

	return 1;
}

GIFIMP void stbi__out_gif_code(stbi__gif* g, uint16_t code)
{
	byte* p, * c;
	int idx;

	if (g->codes[code].prefix >= 0)
		stbi__out_gif_code(g, g->codes[code].prefix);

	if (g->cur_y >= g->max_y) return;

	idx = g->cur_x + g->cur_y;
	p = &g->out[idx];
	g->history[idx / 4] = 1;

	c = &g->color_table[g->codes[code].suffix * 4];
	if (c[3] > 128) {
		p[0] = c[2];
		p[1] = c[1];
		p[2] = c[0];
		p[3] = c[3];
	}
	g->cur_x += 4;

	if (g->cur_x >= g->max_x) {
		g->cur_x = g->start_x;
		g->cur_y += g->step;

		while (g->cur_y >= g->max_y && g->parse > 0) {
			g->step = (1 << g->parse) * g->line_size;
			g->cur_y = g->start_y + (g->step >> 1);
			--g->parse;
		}
	}
}

GIFIMP byte* stbi__process_gif_raster(stbi__context* s, stbi__gif* g)
{
	byte lzw_cs;
	int32_t len, init_code;
	uint32_t first;
	int32_t codesize, codemask, avail, oldcode, bits, valid_bits, clear;
	stbi__gif_lzw* p;

	lzw_cs = stbi__get8(s);
	if (lzw_cs > 12) return NULL;
	clear = 1 << lzw_cs;
	first = 1;
	codesize = lzw_cs + 1;
	codemask = (1 << codesize) - 1;
	bits = 0;
	valid_bits = 0;
	for (init_code = 0; init_code < clear; init_code++) {
		g->codes[init_code].prefix = -1;
		g->codes[init_code].first = (byte)init_code;
		g->codes[init_code].suffix = (byte)init_code;
	}

	avail = clear + 2;
	oldcode = -1;

	len = 0;
	for (;;) {
		if (valid_bits < codesize) {
			if (len == 0) {
				len = stbi__get8(s);
				if (len == 0)
					return g->out;
			}
			--len;
			bits |= (int32_t)stbi__get8(s) << valid_bits;
			valid_bits += 8;
		}
		else {
			int32_t code = bits & codemask;
			bits >>= codesize;
			valid_bits -= codesize;

			if (code == clear) {
				codesize = lzw_cs + 1;
				codemask = (1 << codesize) - 1;
				avail = clear + 2;
				oldcode = -1;
				first = 0;
			}
			else if (code == clear + 1) {
				stbi__skip(s, len);
				while ((len = stbi__get8(s)) > 0)
					stbi__skip(s, len);
				return g->out;
			}
			else if (code <= avail) {
				if (first) {
					return NULL;
				}

				if (oldcode >= 0) {
					p = &g->codes[avail++];
					if (avail > 8192) {
						return NULL;
					}

					p->prefix = (int16_t)oldcode;
					p->first = g->codes[oldcode].first;
					p->suffix = (code == avail) ? p->first : g->codes[code].first;
				}
				else if (code == avail)
					return NULL;

				stbi__out_gif_code(g, (uint16_t)code);

				if ((avail & codemask) == 0 && avail <= 0x0FFF) {
					codesize++;
					codemask = (1 << codesize) - 1;
				}

				oldcode = code;
			}
			else {
				return NULL;
			}
		}
	}
}

GIFIMP byte* stbi__gif_load_next(stbi__context* s, stbi__gif* g, int* comp, int req_comp, byte* two_back)
{
	int dispose;
	int first_frame;
	int pi;
	int pcount;

	first_frame = 0;
	if (g->out == 0)
	{
		if (!stbi__gif_header(s, g, comp, 0)) return 0;
		if (!stbi__mad3sizes_valid(4, g->w, g->h, 0))
			return NULL;
		pcount = g->w * g->h;
		g->out = (byte*)stbi__malloc(4 * pcount);
		g->background = (byte*)stbi__malloc(4 * pcount);
		g->history = (byte*)stbi__malloc(pcount);
		if (!g->out || !g->background || !g->history)
			return NULL;

		memset(g->out, 0x00, 4 * pcount);
		memset(g->background, 0x00, 4 * pcount);
		memset(g->history, 0x00, pcount);
		first_frame = 1;
	}
	else {

		dispose = (g->eflags & 0x1C) >> 2;
		pcount = g->w * g->h;

		if ((dispose == 3) && (two_back == 0))
		{
			dispose = 2;
		}
		if (dispose == 3)
		{
			for (pi = 0; pi < pcount; ++pi)
			{
				if (g->history[pi]) {
					memcpy(&g->out[pi * 4], &two_back[pi * 4], 4);
				}
			}
		}
		else if (dispose == 2)
		{

			for (pi = 0; pi < pcount; ++pi)
			{
				if (g->history[pi]) {
					memcpy(&g->out[pi * 4], &g->background[pi * 4], 4);
				}
			}
		}

		memcpy(g->background, g->out, 4 * g->w * g->h);
	}

	memset(g->history, 0x00, g->w * g->h);

	for (;;)
	{
		int tag = stbi__get8(s);
		switch (tag) {
		case 0x2C:
		{
			int32_t x, y, w, h;
			byte* o;

			x = stbi__get16le(s);
			y = stbi__get16le(s);
			w = stbi__get16le(s);
			h = stbi__get16le(s);
			if (((x + w) > (g->w)) || ((y + h) > (g->h)))
				return NULL;

			g->line_size = g->w * 4;
			g->start_x = x * 4;
			g->start_y = y * g->line_size;
			g->max_x = g->start_x + w * 4;
			g->max_y = g->start_y + h * g->line_size;
			g->cur_x = g->start_x;
			g->cur_y = g->start_y;

			if (w == 0)
				g->cur_y = g->max_y;

			g->lflags = stbi__get8(s);

			if (g->lflags & 0x40) {
				g->step = 8 * g->line_size;
				g->parse = 3;
			}
			else {
				g->step = g->line_size;
				g->parse = 0;
			}

			if (g->lflags & 0x80) {
				stbi__gif_parse_colortable(s, g->lpal, 2 << (g->lflags & 7), g->eflags & 0x01 ? g->transparent : -1);
				g->color_table = (byte*)g->lpal;
			}
			else if (g->flags & 0x80) {
				g->color_table = (byte*)g->pal;
			}
			else
				return NULL;

			o = stbi__process_gif_raster(s, g);
			if (!o) return NULL;


			pcount = g->w * g->h;
			if (first_frame && (g->bgindex > 0)) {

				for (pi = 0; pi < pcount; ++pi) {
					if (g->history[pi] == 0) {
						g->pal[g->bgindex][3] = 255;
						memcpy(&g->out[pi * 4], &g->pal[g->bgindex], 4);
					}
				}
			}

			return o;
		}

		case 0x21:
		{
			int len;
			int ext = stbi__get8(s);
			if (ext == 0xF9) {
				len = stbi__get8(s);
				if (len == 4) {
					g->eflags = stbi__get8(s);
					g->delay = 10 * stbi__get16le(s);


					if (g->transparent >= 0) {
						g->pal[g->transparent][3] = 255;
					}
					if (g->eflags & 0x01) {
						g->transparent = stbi__get8(s);
						if (g->transparent >= 0) {
							g->pal[g->transparent][3] = 0;
						}
					}
					else {

						stbi__skip(s, 1);
						g->transparent = -1;
					}
				}
				else {
					stbi__skip(s, len);
					break;
				}
			}
			while ((len = stbi__get8(s)) != 0) {
				stbi__skip(s, len);
			}
			break;
		}

		case 0x3B:
			return (byte*)s;

		default:
			return NULL;
		}
	}
}

GIFIMP void* stbi__load_gif_main_outofmem(stbi__gif* g, byte* out, int** delays)
{
	free(g->out);
	free(g->history);
	free(g->background);

	if (out) free(out);
	if (delays && *delays) free(*delays);
	return NULL;
}

GIFIMP void* stbi__load_gif_main(stbi__context* s, int** delays, int* x, int* y, int* z, int* comp, int req_comp)
{
	if (stbi__gif_test(s)) {
		int layers = 0;
		byte* u = 0;
		byte* out = 0;
		byte* two_back = 0;
		stbi__gif g;
		int stride;
		int out_size = 0;
		int delays_size = 0;

		memset(&g, 0, sizeof(g));
		if (delays) {
			*delays = 0;
		}

		do {
			u = stbi__gif_load_next(s, &g, comp, req_comp, two_back);
			if (u == (byte*)s) u = 0;

			if (u) {
				*x = g.w;
				*y = g.h;
				++layers;
				stride = g.w * g.h * 4;

				if (out) {
					void* tmp = (byte*)realloc(out, layers * stride);
					if (!tmp)
						return stbi__load_gif_main_outofmem(&g, out, delays);
					else {
						out = (byte*)tmp;
						out_size = layers * stride;
					}

					if (delays) {
						int* new_delays = (int*)realloc(*delays, sizeof(int) * layers);
						if (!new_delays)
							return stbi__load_gif_main_outofmem(&g, out, delays);
						*delays = new_delays;
						delays_size = layers * sizeof(int);
					}
				}
				else {
					out = (byte*)stbi__malloc(layers * stride);
					if (!out)
						return stbi__load_gif_main_outofmem(&g, out, delays);
					out_size = layers * stride;
					if (delays) {
						*delays = (int*)stbi__malloc(layers * sizeof(int));
						if (!*delays)
							return stbi__load_gif_main_outofmem(&g, out, delays);
						delays_size = layers * sizeof(int);
					}
				}
				memcpy(out + ((layers - 1) * stride), u, stride);
				if (layers >= 2) {
					two_back = out - 2 * stride;
				}

				if (delays) {
					(*delays)[layers - 1U] = g.delay;
				}
			}
		} while (u != 0);

		free(g.out);
		free(g.history);
		free(g.background);

		if (req_comp && req_comp != 4)
			out = stbi__convert_format(out, 4, req_comp, layers * g.w, g.h);

		*z = layers;
		return out;
	}
	else {
		return NULL;
	}
}

GIFIMP byte* stbi_load_gif_from_memory(byte const* buffer, int len, int** delays, int* x, int* y, int* z, int* comp, int req_comp)
{
	unsigned char* result;
	stbi__context s;
	stbi__start_mem(&s, buffer, len);

	result = (unsigned char*)stbi__load_gif_main(&s, delays, x, y, z, comp, req_comp);
	//if (stbi__vertically_flip_on_load) {
	//	stbi__vertical_flip_slices(result, *x, *y, *z, *comp);
	//}

	return result;
}