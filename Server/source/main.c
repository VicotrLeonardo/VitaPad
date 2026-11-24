/*
 * Vita Remote - PS Vita como gamepad, mouse e teclado para Linux.
 * Baseado no VitaPad de Rinnegatamante.
 *
 * Troca de modo: toque nas abas do topo ou SELECT+START.
 */
#include <stdio.h>
#include <string.h>
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>

#include "net.h"
#include "protocol.h"

#define SCREEN_W 960
#define SCREEN_H 544
#define STATUS_H 40
#define TAB_W    (SCREEN_W / VR_MODE_COUNT)

#define COLOR_BG        RGBA8(0x12, 0x14, 0x1A, 0xFF)
#define COLOR_PANEL     RGBA8(0x1E, 0x22, 0x2B, 0xFF)
#define COLOR_ACCENT    RGBA8(0x3D, 0x8B, 0xFF, 0xFF)
#define COLOR_TEXT      RGBA8(0xE8, 0xEA, 0xEE, 0xFF)
#define COLOR_MUTED     RGBA8(0x8A, 0x90, 0x9C, 0xFF)
#define COLOR_OK        RGBA8(0x4C, 0xC9, 0x6E, 0xFF)
#define COLOR_WAIT      RGBA8(0xF2, 0xB8, 0x3C, 0xFF)
#define COLOR_FINGER    RGBA8(0x3D, 0x8B, 0xFF, 0xA0)

#define VERSION_STR "Vita Remote v0.2"

static const char *MODE_NAMES[VR_MODE_COUNT] = { "GAMEPAD", "MOUSE", "TECLADO" };
static vita2d_pgf *font;

static void text(int x, int y, uint32_t color, float scale, const char *s) {
	vita2d_pgf_draw_text(font, x, y, color, scale, s);
}

static void text_centered(int cx, int y, uint32_t color, float scale, const char *s) {
	int w = vita2d_pgf_text_width(font, scale, s);
	text(cx - w / 2, y, color, scale, s);
}

static void rect_outline(int x, int y, int w, int h, uint32_t color) {
	vita2d_draw_line(x, y, x + w, y, color);
	vita2d_draw_line(x + w, y, x + w, y + h, color);
	vita2d_draw_line(x + w, y + h, x, y + h, color);
	vita2d_draw_line(x, y + h, x, y, color);
}

static void draw_tabs(int mode) {
	for (int i = 0; i < VR_MODE_COUNT; i++) {
		int x = i * TAB_W;
		int active = (i == mode);
		vita2d_draw_rectangle(x, 0, TAB_W, UI_TAB_BAR_H, active ? COLOR_ACCENT : COLOR_PANEL);
		if (i > 0)
			vita2d_draw_line(x, 0, x, UI_TAB_BAR_H, COLOR_BG);
		text_centered(x + TAB_W / 2, 42, active ? COLOR_TEXT : COLOR_MUTED, 1.2f, MODE_NAMES[i]);
	}
}

/* Mostra a posição de um analógico (0..255) num círculo. */
static void draw_stick(int cx, int cy, uint8_t sx, uint8_t sy, const char *label) {
	vita2d_draw_fill_circle(cx, cy, 60, COLOR_PANEL);
	vita2d_draw_fill_circle(cx + (sx - 128) * 50 / 128, cy + (sy - 128) * 50 / 128, 14, COLOR_ACCENT);
	text_centered(cx, cy + 90, COLOR_MUTED, 1.0f, label);
}

static void draw_button_dot(int x, int y, const char *label, int on) {
	vita2d_draw_fill_circle(x, y, 22, on ? COLOR_ACCENT : COLOR_PANEL);
	text_centered(x, y + 8, COLOR_TEXT, 0.9f, label);
}

static void draw_gamepad(const SceCtrlData *pad) {
	int top = UI_TAB_BAR_H;
	text(30, top + 50, COLOR_TEXT, 1.0f, "Botoes e analogicos viram um controle (layout Xbox) no PC.");
	text(30, top + 78, COLOR_MUTED, 0.9f, "Tela: touchpad   |   Toque traseiro esquerdo/direito: LT/RT");

	draw_stick(200, top + 220, pad->lx, pad->ly, "L");
	draw_stick(760, top + 220, pad->rx, pad->ry, "R");

	uint32_t b = pad->buttons;
	int cx = 480, cy = top + 220;
	draw_button_dot(cx, cy - 60, "/\\", b & SCE_CTRL_TRIANGLE);
	draw_button_dot(cx - 60, cy, "[]", b & SCE_CTRL_SQUARE);
	draw_button_dot(cx + 60, cy, "O", b & SCE_CTRL_CIRCLE);
	draw_button_dot(cx, cy + 60, "X", b & SCE_CTRL_CROSS);
	draw_button_dot(cx - 150, top + 120, "L", b & SCE_CTRL_LTRIGGER);
	draw_button_dot(cx + 150, top + 120, "R", b & SCE_CTRL_RTRIGGER);
}

static void draw_mouse(const SceTouchData *touch) {
	int top = UI_TAB_BAR_H + 10;
	int h = SCREEN_H - STATUS_H - top - 10;
	rect_outline(20, top, SCREEN_W - 40, h, COLOR_MUTED);
	text_centered(SCREEN_W / 2, top + h / 2 - 20, COLOR_MUTED, 1.0f, "1 dedo: mover   |   toque rapido: clique");
	text_centered(SCREEN_W / 2, top + h / 2 + 10, COLOR_MUTED, 1.0f, "2 dedos: rolar   |   toque com 2 dedos: clique direito");
	text_centered(SCREEN_W / 2, top + h / 2 + 40, COLOR_MUTED, 0.9f, "X: clique   O: clique direito   Analogico direito: rolar");
	for (unsigned i = 0; i < touch->reportNum; i++) {
		int x = touch->report[i].x / 2, y = touch->report[i].y / 2;
		if (y >= UI_TAB_BAR_H)
			vita2d_draw_fill_circle(x, y, 26, COLOR_FINGER);
	}
}

static void draw_keyboard(void) {
	int top = UI_TAB_BAR_H;
	text_centered(SCREEN_W / 2, top + 180, COLOR_TEXT, 1.3f, "Teclado visual");
	text_centered(SCREEN_W / 2, top + 220, COLOR_MUTED, 1.0f, "Chega na Fase 3 (layout ABNT2).");
	text_centered(SCREEN_W / 2, top + 250, COLOR_MUTED, 1.0f, "Os botoes fisicos seguem o perfil 'keyboard' do PC.");
}

static void draw_status(void) {
	int y = SCREEN_H - STATUS_H;
	vita2d_draw_rectangle(0, y, SCREEN_W, STATUS_H, COLOR_PANEL);
	char buf[96];
	snprintf(buf, sizeof(buf), "Vita: %s", net_local_ip());
	text(16, y + 27, COLOR_MUTED, 1.0f, buf);
	if (net_connected()) {
		snprintf(buf, sizeof(buf), "Conectado: %s", net_peer_ip());
		text_centered(SCREEN_W / 2, y + 27, COLOR_OK, 1.0f, buf);
	} else {
		text_centered(SCREEN_W / 2, y + 27, COLOR_WAIT, 1.0f, "Aguardando o PC...");
	}
	const char *hint = "SELECT+START: modo";
	text(SCREEN_W - 16 - vita2d_pgf_text_width(font, 0.9f, hint), y + 27, COLOR_MUTED, 0.9f, hint);
}

int main(void) {
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

	vita2d_init();
	vita2d_set_clear_color(COLOR_BG);
	font = vita2d_load_default_pgf();

	net_start();

	const uint32_t combo = SCE_CTRL_SELECT | SCE_CTRL_START;
	int was_touching = 0;
	int combo_latched = 0;

	for (;;) {
		/* Sem isso o Vita entra em suspensão enquanto você joga só pelo PC. */
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);

		SceCtrlData pad;
		SceTouchData touch;
		sceCtrlPeekBufferPositive(0, &pad, 1);
		sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

		/* Toque numa aba troca o modo (só no início do toque). */
		int touching = touch.reportNum > 0;
		if (touching && !was_touching) {
			int x = touch.report[0].x / 2, y = touch.report[0].y / 2;
			if (y < UI_TAB_BAR_H)
				net_set_mode(x / TAB_W);
		}
		was_touching = touching;

		/* SELECT+START: próximo modo. Os dois botões não vão para o PC até serem soltos. */
		if ((pad.buttons & combo) == combo) {
			net_set_suppressed_buttons(combo);
			if (!combo_latched) {
				combo_latched = 1;
				net_set_mode((net_get_mode() + 1) % VR_MODE_COUNT);
			}
		} else if (!(pad.buttons & combo)) {
			combo_latched = 0;
			net_set_suppressed_buttons(0);
		}

		int mode = net_get_mode();
		vita2d_start_drawing();
		vita2d_clear_screen();
		draw_tabs(mode);
		if (mode == VR_MODE_GAMEPAD)
			draw_gamepad(&pad);
		else if (mode == VR_MODE_MOUSE)
			draw_mouse(&touch);
		else
			draw_keyboard();
		draw_status();
		text(SCREEN_W - 16 - vita2d_pgf_text_width(font, 0.7f, VERSION_STR), UI_TAB_BAR_H + 22, COLOR_MUTED, 0.7f, VERSION_STR);
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();
	}
	return 0;
}
