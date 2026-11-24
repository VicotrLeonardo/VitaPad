#include <math.h>
#include <psp2/kernel/processmgr.h>
#include "ui.h"

static vita2d_pvf *font;
static int cap_h_base; /* altura de uma maiúscula com escala 1.0 */

void ui_init(void) {
	font = vita2d_load_default_pvf();
	cap_h_base = vita2d_pvf_text_height(font, 1.0f, "H");
	if (cap_h_base <= 0)
		cap_h_base = 16;
}

uint64_t ui_time_us(void) { return sceKernelGetProcessTimeWide(); }

uint32_t ui_alpha(uint32_t color, uint8_t alpha) {
	return (color & 0x00FFFFFF) | ((uint32_t)alpha << 24);
}

/* ------------------------------------------------------------ texto */

int ui_text_w(float scale, const char *s) { return vita2d_pvf_text_width(font, scale, s); }

void ui_text(int x, int y, uint32_t color, float scale, const char *s) {
	vita2d_pvf_draw_text(font, x, y, color, scale, s);
}

void ui_text_mid(int x, int cy, uint32_t color, float scale, const char *s) {
	ui_text(x, cy + (int)(cap_h_base * scale) / 2, color, scale, s);
}

void ui_text_center(int cx, int cy, uint32_t color, float scale, const char *s) {
	ui_text_mid(cx - ui_text_w(scale, s) / 2, cy, color, scale, s);
}

void ui_text_right(int right, int cy, uint32_t color, float scale, const char *s) {
	ui_text_mid(right - ui_text_w(scale, s), cy, color, scale, s);
}

/* ------------------------------------------------------------ formas */

static vita2d_color_vertex *verts(int n) {
	return vita2d_pool_memalign(n * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
}

static void set_v(vita2d_color_vertex *v, float x, float y, uint32_t color) {
	v->x = x;
	v->y = y;
	v->z = 0.5f;
	v->color = color;
}

void ui_triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color) {
	vita2d_color_vertex *v = verts(3);
	if (!v)
		return;
	set_v(&v[0], x0, y0, color);
	set_v(&v[1], x1, y1, color);
	set_v(&v[2], x2, y2, color);
	vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, 3);
}

/* Retângulo arredondado num único leque de triângulos (~30 vértices). Bem mais leve
 * que 4 círculos: o teclado desenha ~80 deles por quadro. */
void ui_rrect(float x, float y, float w, float h, float r, uint32_t color) {
	enum { SEG = 6 };
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	if (r < 1) {
		vita2d_draw_rectangle(x, y, w, h, color);
		return;
	}
	vita2d_color_vertex *v = verts(2 + 4 * (SEG + 1));
	if (!v)
		return;
	const float cx[4] = { x + w - r, x + r, x + r, x + w - r };
	const float cy[4] = { y + h - r, y + h - r, y + r, y + r };
	int n = 0;
	set_v(&v[n++], x + w / 2, y + h / 2, color);
	for (int c = 0; c < 4; c++) {
		for (int i = 0; i <= SEG; i++) {
			float a = ((float)c + (float)i / SEG) * (float)M_PI / 2;
			set_v(&v[n++], cx[c] + cosf(a) * r, cy[c] + sinf(a) * r, color);
		}
	}
	v[n] = v[1];
	n++;
	vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_FAN, v, n);
}

void ui_rrect_border(float x, float y, float w, float h, float r, float border, uint32_t fill, uint32_t line) {
	ui_rrect(x, y, w, h, r, line);
	ui_rrect(x + border, y + border, w - 2 * border, h - 2 * border, r > border ? r - border : 0, fill);
}

void ui_line(float x0, float y0, float x1, float y1, float width, uint32_t color) {
	float dx = x1 - x0, dy = y1 - y0;
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.001f)
		return;
	float nx = -dy / len * width / 2, ny = dx / len * width / 2;
	vita2d_color_vertex *v = verts(6);
	if (!v)
		return;
	set_v(&v[0], x0 + nx, y0 + ny, color);
	set_v(&v[1], x0 - nx, y0 - ny, color);
	set_v(&v[2], x1 + nx, y1 + ny, color);
	set_v(&v[3], x1 + nx, y1 + ny, color);
	set_v(&v[4], x0 - nx, y0 - ny, color);
	set_v(&v[5], x1 - nx, y1 - ny, color);
	vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, 6);
}

void ui_ring(float cx, float cy, float radius, float width, uint32_t color) {
	enum { SEG = 32 };
	vita2d_color_vertex *v = verts((SEG + 1) * 2);
	if (!v)
		return;
	float r_out = radius, r_in = radius - width;
	for (int i = 0; i <= SEG; i++) {
		float a = (float)i / SEG * 2.0f * (float)M_PI;
		float c = cosf(a), s = sinf(a);
		set_v(&v[i * 2], cx + c * r_out, cy + s * r_out, color);
		set_v(&v[i * 2 + 1], cx + c * r_in, cy + s * r_in, color);
	}
	vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, (SEG + 1) * 2);
}

uint32_t ui_glyph_color(Glyph g) {
	switch (g) {
	case GLYPH_TRIANGLE: return C_TRIANGLE;
	case GLYPH_CIRCLE:   return C_CIRCLE;
	case GLYPH_CROSS:    return C_CROSS;
	default:             return C_SQUARE;
	}
}

void ui_glyph(Glyph g, float cx, float cy, float size, uint32_t color) {
	float h = size / 2, w = size * 0.13f;
	if (w < 2) w = 2;
	switch (g) {
	case GLYPH_CIRCLE:
		ui_ring(cx, cy, h, w, color);
		break;
	case GLYPH_CROSS:
		ui_line(cx - h, cy - h, cx + h, cy + h, w, color);
		ui_line(cx - h, cy + h, cx + h, cy - h, w, color);
		break;
	case GLYPH_SQUARE: {
		float s = h * 0.88f;
		ui_line(cx - s - w / 2, cy - s, cx + s + w / 2, cy - s, w, color);
		ui_line(cx - s - w / 2, cy + s, cx + s + w / 2, cy + s, w, color);
		ui_line(cx - s, cy - s, cx - s, cy + s, w, color);
		ui_line(cx + s, cy - s, cx + s, cy + s, w, color);
		break;
	}
	case GLYPH_TRIANGLE: {
		float top = cy - h, bottom = cy + h * 0.75f;
		ui_line(cx, top, cx + h, bottom, w, color);
		ui_line(cx + h, bottom, cx - h, bottom, w, color);
		ui_line(cx - h, bottom, cx, top, w, color);
		break;
	}
	}
}

void ui_arrow(float cx, float cy, float s, int dir, uint32_t color) {
	float h = s / 2;
	switch (dir) {
	case 0: ui_triangle(cx, cy - h, cx + h, cy + h * 0.6f, cx - h, cy + h * 0.6f, color); break;
	case 1: ui_triangle(cx + h, cy, cx - h * 0.6f, cy + h, cx - h * 0.6f, cy - h, color); break;
	case 2: ui_triangle(cx, cy + h, cx - h, cy - h * 0.6f, cx + h, cy - h * 0.6f, color); break;
	default: ui_triangle(cx - h, cy, cx + h * 0.6f, cy - h, cx + h * 0.6f, cy + h, color); break;
	}
}

/* ------------------------------------------------------------ legendas */

int ui_legend_glyph(int x, int cy, Glyph g, const char *label) {
	ui_rrect(x, cy - 12, 24, 24, 12, C_SURFACE3);
	ui_glyph(g, x + 12, cy, 11, ui_glyph_color(g));
	ui_text_mid(x + 32, cy, C_MUTED, T_SMALL, label);
	return 32 + ui_text_w(T_SMALL, label) + 22;
}

int ui_legend_key(int x, int cy, const char *key, const char *label) {
	int kw = ui_text_w(T_SMALL * 0.9f, key) + 16;
	ui_rrect(x, cy - 12, kw, 24, 6, C_SURFACE3);
	ui_text_center(x + kw / 2, cy, C_TEXT, T_SMALL * 0.9f, key);
	ui_text_mid(x + kw + 8, cy, C_MUTED, T_SMALL, label);
	return kw + 8 + ui_text_w(T_SMALL, label) + 22;
}
