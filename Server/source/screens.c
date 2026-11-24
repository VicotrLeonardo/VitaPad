#include <math.h>
#include <stdio.h>

#include "net.h"
#include "protocol.h"
#include "screens.h"
#include "ui.h"

#define VERSION_STR "v0.3"

/* Ordem das abas na tela. Os valores do protocolo (VR_MODE_*) não mudam. */
static const int TAB_ORDER[TAB_COUNT] = { VR_MODE_MOUSE, VR_MODE_KEYBOARD, VR_MODE_GAMEPAD };
static const char *TAB_NAMES[TAB_COUNT] = { "Mouse", "Teclado", "Gamepad" };

int screens_tab_at(int x, int y) {
	if (y >= HEADER_H || x < TABS_X || x >= TABS_X + TABS_W)
		return -1;
	return TAB_ORDER[(x - TABS_X) * TAB_COUNT / TABS_W];
}

int screens_next_mode(int mode) {
	for (int i = 0; i < TAB_COUNT; i++)
		if (TAB_ORDER[i] == mode)
			return TAB_ORDER[(i + 1) % TAB_COUNT];
	return TAB_ORDER[0];
}

int screens_in_scroll_zone(int x, int y) {
	return y >= BODY_Y && y < SCREEN_H - FOOTER_H && x >= SCROLL_ZONE_X && x < SCROLL_ZONE_X + SCROLL_ZONE_W;
}

/* ------------------------------------------------------------ cabeçalho */

static void tab_icon(int mode, int cx, int cy, uint32_t c) {
	switch (mode) {
	case VR_MODE_GAMEPAD: /* controle */
		ui_rrect(cx - 13, cy - 7, 26, 14, 7, c);
		vita2d_draw_fill_circle(cx - 6, cy, 2.5f, C_SURFACE);
		vita2d_draw_fill_circle(cx + 6, cy, 2.5f, C_SURFACE);
		break;
	case VR_MODE_MOUSE: /* mouse */
		ui_rrect(cx - 7, cy - 10, 14, 20, 7, c);
		vita2d_draw_rectangle(cx - 1, cy - 9, 2, 6, C_SURFACE);
		break;
	default: /* teclado */
		ui_rrect(cx - 13, cy - 8, 26, 16, 3, c);
		for (int i = 0; i < 4; i++)
			vita2d_draw_rectangle(cx - 10 + i * 6, cy - 5, 3, 3, C_SURFACE);
		vita2d_draw_rectangle(cx - 7, cy + 2, 14, 3, C_SURFACE);
		break;
	}
}

static void draw_connection_pill(void) {
	char buf[48];
	int right = SCREEN_W - 16, cy = HEADER_H / 2;
	uint32_t dot;
	if (net_connected()) {
		snprintf(buf, sizeof(buf), "PC %s", net_peer_ip());
		dot = C_OK;
	} else {
		snprintf(buf, sizeof(buf), "Procurando PC");
		/* Ponto pulsando enquanto espera. */
		float t = (ui_time_us() % 1200000) / 1200000.0f;
		dot = ui_alpha(C_WARN, (uint8_t)(110 + 145 * (0.5f + 0.5f * sinf(t * 2 * (float)M_PI))));
	}
	int w = ui_text_w(T_SMALL, buf) + 40;
	ui_rrect(right - w, cy - 16, w, 32, 16, C_SURFACE2);
	vita2d_draw_fill_circle(right - w + 16, cy, 5, dot);
	ui_text_mid(right - w + 28, cy, net_connected() ? C_TEXT : C_MUTED, T_SMALL, buf);
}

void screens_draw_header(int mode) {
	vita2d_draw_rectangle(0, 0, SCREEN_W, HEADER_H, C_SURFACE);
	vita2d_draw_rectangle(0, HEADER_H - 1, SCREEN_W, 1, C_BORDER);

	ui_text_mid(20, HEADER_H / 2 - 7, C_TEXT, T_LABEL, "Vita Remote");
	ui_text_mid(20, HEADER_H / 2 + 13, C_FAINT, T_SMALL * 0.85f, VERSION_STR);

	/* Controle segmentado */
	int seg_w = TABS_W / TAB_COUNT;
	ui_rrect(TABS_X, 10, TABS_W, HEADER_H - 20, 20, C_BG);
	for (int i = 0; i < TAB_COUNT; i++) {
		int x = TABS_X + i * seg_w;
		int active = TAB_ORDER[i] == mode;
		if (active)
			ui_rrect(x + 3, 13, seg_w - 6, HEADER_H - 26, 17, C_ACCENT);
		uint32_t fg = active ? C_TEXT : C_MUTED;
		int label_w = ui_text_w(T_BODY, TAB_NAMES[i]);
		int content_w = 26 + 10 + label_w;
		int cx0 = x + (seg_w - content_w) / 2;
		tab_icon(TAB_ORDER[i], cx0 + 13, HEADER_H / 2, fg);
		ui_text_mid(cx0 + 36, HEADER_H / 2, fg, T_BODY, TAB_NAMES[i]);
	}
	draw_connection_pill();
}

/* ------------------------------------------------------------ rodapé */

static int legend_key_w(const char *key, const char *label) {
	return ui_text_w(T_SMALL * 0.9f, key) + 16 + 8 + ui_text_w(T_SMALL, label);
}

void screens_draw_footer(int mode) {
	int y = SCREEN_H - FOOTER_H, cy = y + FOOTER_H / 2;
	vita2d_draw_rectangle(0, y, SCREEN_W, FOOTER_H, C_SURFACE);
	vita2d_draw_rectangle(0, y, SCREEN_W, 1, C_BORDER);

	int x = 16;
	if (mode == VR_MODE_GAMEPAD) {
		x += ui_legend_glyph(x, cy, GLYPH_CROSS, "A");
		x += ui_legend_glyph(x, cy, GLYPH_CIRCLE, "B");
		x += ui_legend_glyph(x, cy, GLYPH_SQUARE, "X");
		x += ui_legend_glyph(x, cy, GLYPH_TRIANGLE, "Y");
		x += ui_legend_key(x, cy, "Tela", "Touchpad");
	} else if (mode == VR_MODE_MOUSE) {
		x += ui_legend_glyph(x, cy, GLYPH_CROSS, "Clique");
		x += ui_legend_glyph(x, cy, GLYPH_CIRCLE, "Direito");
		x += ui_legend_glyph(x, cy, GLYPH_SQUARE, "Meio");
		x += ui_legend_key(x, cy, "L / R", "Voltar / Avançar");
	} else {
		x += ui_legend_key(x, cy, "D-pad", "Navegar");
		x += ui_legend_glyph(x, cy, GLYPH_CROSS, "Tecla");
		x += ui_legend_glyph(x, cy, GLYPH_CIRCLE, "Apagar");
		x += ui_legend_glyph(x, cy, GLYPH_SQUARE, "Espaço");
		x += ui_legend_glyph(x, cy, GLYPH_TRIANGLE, "Shift");
		x += ui_legend_key(x, cy, "START", "Enter");
	}
	const char *k = "SELECT + START", *l = "Trocar modo";
	ui_legend_key(SCREEN_W - 16 - legend_key_w(k, l), cy, k, l);
}

/* ------------------------------------------------------------ aguardando */

static void draw_wait_card(int cy) {
	int w = 560, h = 112, x = (SCREEN_W - w) / 2, y = cy - h / 2;
	ui_rrect_border(x, y, w, h, 16, 1, C_SURFACE, C_BORDER);
	ui_text_center(SCREEN_W / 2, y + 28, C_TEXT, T_LABEL, "Aguardando o PC");
	ui_text_center(SCREEN_W / 2, y + 58, C_MUTED, T_BODY, "No PC, rode:  python3 -m vitaremote");
	char buf[64];
	snprintf(buf, sizeof(buf), "IP deste Vita: %s", net_local_ip());
	ui_text_center(SCREEN_W / 2, y + 86, C_FAINT, T_SMALL, buf);
}

/* ------------------------------------------------------------ gamepad */

static void stick(int cx, int cy, uint8_t sx, uint8_t sy, const char *label) {
	ui_ring(cx, cy, 58, 2, C_BORDER);
	vita2d_draw_fill_circle(cx, cy, 56, C_SURFACE);
	vita2d_draw_rectangle(cx - 40, cy, 80, 1, C_SURFACE3);
	vita2d_draw_rectangle(cx, cy - 40, 1, 80, C_SURFACE3);
	float dx = (sx - 128) / 128.0f, dy = (sy - 128) / 128.0f;
	int moved = fabsf(dx) > 0.15f || fabsf(dy) > 0.15f;
	vita2d_draw_fill_circle(cx + dx * 36, cy + dy * 36, 22, moved ? C_ACCENT : C_SURFACE3);
	ui_text_center(cx, cy + 78, C_MUTED, T_SMALL, label);
}

static void face_button(int cx, int cy, Glyph g, int on) {
	uint32_t c = ui_glyph_color(g);
	vita2d_draw_fill_circle(cx, cy, 25, on ? c : C_SURFACE2);
	ui_glyph(g, cx, cy, 20, on ? C_BG : c);
}

static void dpad_button(int cx, int cy, int dir, int on) {
	ui_rrect(cx - 21, cy - 21, 42, 42, 8, on ? C_ACCENT : C_SURFACE2);
	ui_arrow(cx, cy, 14, dir, on ? C_TEXT : C_MUTED);
}

static void shoulder(int x, int y, int w, const char *name, const char *xbox, int on) {
	ui_rrect(x, y, w, 36, 12, on ? C_ACCENT : C_SURFACE2);
	ui_text_mid(x + 14, y + 18, on ? C_TEXT : C_TEXT, T_BODY, name);
	ui_text_right(x + w - 14, y + 18, on ? C_TEXT : C_FAINT, T_SMALL, xbox);
}

static void small_pill(int cx, int cy, const char *label, int on) {
	int w = ui_text_w(T_SMALL, label) + 24;
	ui_rrect(cx - w / 2, cy - 13, w, 26, 13, on ? C_ACCENT : C_SURFACE2);
	ui_text_center(cx, cy, on ? C_TEXT : C_MUTED, T_SMALL, label);
}

void screens_draw_gamepad(const SceCtrlData *pad, const SceTouchData *rear) {
	uint32_t b = pad->buttons;
	int rear_l = 0, rear_r = 0;
	for (unsigned i = 0; i < rear->reportNum; i++) {
		if (rear->report[i].x < 960) rear_l = 1;
		else rear_r = 1;
	}

	int top = BODY_Y + 18;
	shoulder(40, top, 230, "L", "LB", b & SCE_CTRL_LTRIGGER);
	shoulder(SCREEN_W - 270, top, 230, "R", "RB", b & SCE_CTRL_RTRIGGER);
	small_pill(SCREEN_W / 2 - 90, top + 18, "LT · traseira esq.", rear_l);
	small_pill(SCREEN_W / 2 + 90, top + 18, "RT · traseira dir.", rear_r);

	/* D-pad */
	int dx = 175, dy = BODY_Y + 175;
	dpad_button(dx, dy - 46, 0, b & SCE_CTRL_UP);
	dpad_button(dx, dy + 46, 2, b & SCE_CTRL_DOWN);
	dpad_button(dx - 46, dy, 3, b & SCE_CTRL_LEFT);
	dpad_button(dx + 46, dy, 1, b & SCE_CTRL_RIGHT);

	/* Botões de ação */
	int fx = SCREEN_W - 175, fy = BODY_Y + 175;
	face_button(fx, fy - 50, GLYPH_TRIANGLE, b & SCE_CTRL_TRIANGLE);
	face_button(fx - 50, fy, GLYPH_SQUARE, b & SCE_CTRL_SQUARE);
	face_button(fx + 50, fy, GLYPH_CIRCLE, b & SCE_CTRL_CIRCLE);
	face_button(fx, fy + 50, GLYPH_CROSS, b & SCE_CTRL_CROSS);

	/* Select / Start */
	small_pill(SCREEN_W / 2 - 55, BODY_Y + 175, "SELECT", b & SCE_CTRL_SELECT);
	small_pill(SCREEN_W / 2 + 55, BODY_Y + 175, "START", b & SCE_CTRL_START);

	/* Analógicos */
	stick(300, BODY_Y + 320, pad->lx, pad->ly, "Analógico esquerdo");
	stick(SCREEN_W - 300, BODY_Y + 320, pad->rx, pad->ry, "Analógico direito");

	if (!net_connected())
		draw_wait_card(BODY_Y + 330);
}

/* ------------------------------------------------------------ mouse */

static void gesture_row(int x, int cy, int fingers, const char *label) {
	for (int i = 0; i < fingers; i++)
		vita2d_draw_fill_circle(x + 10 + i * 16, cy, 7, C_FAINT);
	ui_text_mid(x + 50, cy, C_MUTED, T_BODY, label);
}

static void scroll_strip(int y, int h, int active) {
	int x = SCROLL_ZONE_X + 8, w = SCROLL_ZONE_W - 16;
	ui_rrect(x, y + 10, w, h - 20, 14, active ? C_ACCENT_DIM : C_SURFACE2);
	int cx = x + w / 2;
	uint32_t c = active ? C_TEXT : C_MUTED;
	ui_arrow(cx, y + 42, 18, 0, c);
	ui_arrow(cx, y + h - 42, 18, 2, c);
	for (int i = -2; i <= 2; i++) /* "trilho" pontilhado */
		vita2d_draw_rectangle(cx - 1, y + h / 2 + i * 22 - 6, 2, 12, active ? C_ACCENT : C_SURFACE3);
	ui_text_center(cx, y + h / 2 + 76, c, T_SMALL, "Rolar");
}

void screens_draw_mouse(const SceTouchData *front, int scrolling) {
	int x = 20, y = BODY_Y + 14, w = SCREEN_W - 40, h = BODY_H - 28;
	ui_rrect_border(x, y, w, h, 18, 1, C_SURFACE, C_BORDER);

	/* Grade de pontos: dá a sensação de superfície de touchpad (fora da faixa de rolagem). */
	for (int gy = y + 30; gy < y + h - 10; gy += 40)
		for (int gx = x + 30; gx < x + w - 10; gx += 40)
			if (gx < SCROLL_ZONE_X - 4 || gx > SCROLL_ZONE_X + SCROLL_ZONE_W + 4)
				vita2d_draw_rectangle(gx, gy, 2, 2, C_SURFACE3);
	scroll_strip(y, h, scrolling);

	int fingers = 0;
	for (unsigned i = 0; i < front->reportNum; i++) {
		int fx = front->report[i].x / 2, fy = front->report[i].y / 2;
		if (fy < HEADER_H)
			continue;
		fingers++;
		vita2d_draw_fill_circle(fx, fy, 40, C_ACCENT_DIM);
		ui_ring(fx, fy, 30, 3, C_ACCENT);
	}

	if (!net_connected()) {
		draw_wait_card(y + h / 2);
	} else if (fingers == 0) {
		/* Dicas, uma de cada lado da faixa de rolagem. */
		int lx = x + 40, rx = SCROLL_ZONE_X + SCROLL_ZONE_W + 40, gy = y + h / 2 - 30;
		gesture_row(lx, gy, 1, "Arraste: mover");
		gesture_row(lx, gy + 40, 1, "Toque rápido: clique");
		gesture_row(rx, gy, 2, "Dois dedos: rolar");
		gesture_row(rx, gy + 40, 2, "Toque com 2: direito");
	} else if (fingers >= 2 && !scrolling) {
		small_pill(SCREEN_W / 2, y + 28, "Rolando", 1);
	}
}
