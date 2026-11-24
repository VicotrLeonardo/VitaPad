/*
 * Vita Remote - PS Vita como gamepad, mouse e teclado para Linux.
 * Baseado no VitaPad de Rinnegatamante.
 *
 * Troca de modo: toque nas abas do topo ou SELECT+START.
 */
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>

#include "keyboard.h"
#include "net.h"
#include "protocol.h"
#include "screens.h"
#include "ui.h"

_Static_assert(UI_TAB_BAR_H == HEADER_H, "net.h e ui.h precisam concordar na altura do cabeçalho");

/* Algum dedo na tela é novo neste quadro? (compara os ids com o quadro anterior) */
static int touching_started_now(const SceTouchData *front) {
	static uint8_t prev_ids[8];
	static unsigned prev_n = 0;
	int started = 0;
	for (unsigned i = 0; i < front->reportNum; i++) {
		int seen = 0;
		for (unsigned j = 0; j < prev_n; j++)
			if (prev_ids[j] == front->report[i].id)
				seen = 1;
		if (!seen)
			started = 1;
	}
	prev_n = front->reportNum < 8 ? front->reportNum : 8;
	for (unsigned i = 0; i < prev_n; i++)
		prev_ids[i] = front->report[i].id;
	return started;
}

int main(void) {
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

	vita2d_init_advanced(4 * 1024 * 1024); /* memória de desenho por quadro */
	vita2d_set_clear_color(C_BG);
	ui_init();
	kb_init();
	net_start();

	const uint32_t combo = SCE_CTRL_SELECT | SCE_CTRL_START;
	int was_touching = 0;
	int combo_latched = 0;
	int last_mode = net_get_mode();
	int was_connected = 0;
	int scroll_id = -1; /* id do dedo que começou na faixa de rolagem (-1 = nenhum) */

	for (;;) {
		/* Sem isso o Vita entra em suspensão enquanto você usa só o PC. */
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);

		SceCtrlData pad;
		SceTouchData front, rear;
		sceCtrlPeekBufferPositive(0, &pad, 1);
		sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1);
		sceTouchPeek(SCE_TOUCH_PORT_BACK, &rear, 1);

		/* Toque numa aba troca o modo (só no início do toque). */
		int touching = front.reportNum > 0;
		if (touching && !was_touching) {
			int tab = screens_tab_at(front.report[0].x / 2, front.report[0].y / 2);
			if (tab >= 0)
				net_set_mode(tab);
		}
		was_touching = touching;

		/* SELECT+START: próximo modo. Os dois botões não vão para o PC até serem soltos. */
		if ((pad.buttons & combo) == combo) {
			net_set_suppressed_buttons(combo);
			if (!combo_latched) {
				combo_latched = 1;
				net_set_mode(screens_next_mode(net_get_mode()));
			}
		} else if (!(pad.buttons & combo)) {
			combo_latched = 0;
			net_set_suppressed_buttons(0);
		}

		int mode = net_get_mode();
		int connected = net_connected();
		/* Saiu do teclado ou o PC caiu: solta modificadores travados e teclas seguras. */
		if ((last_mode == VR_MODE_KEYBOARD && mode != VR_MODE_KEYBOARD) || (was_connected && !connected))
			kb_reset();
		last_mode = mode;
		was_connected = connected;
		if (mode == VR_MODE_KEYBOARD)
			kb_update(&front, &pad);

		/* Faixa de rolagem: o dedo que *começa* nela só rola até ser levantado. */
		int new_touch = touching_started_now(&front); /* roda todo quadro: guarda os ids */
		int still_down = 0;
		for (unsigned i = 0; i < front.reportNum; i++)
			if (front.report[i].id == scroll_id)
				still_down = 1;
		if (!still_down)
			scroll_id = -1;
		if (mode == VR_MODE_MOUSE && scroll_id < 0 && front.reportNum == 1 && new_touch)
			if (screens_in_scroll_zone(front.report[0].x / 2, front.report[0].y / 2))
				scroll_id = front.report[0].id;
		if (mode != VR_MODE_MOUSE)
			scroll_id = -1;
		net_set_state_flags(scroll_id >= 0 ? VR_FLAG_SCROLL_ZONE : 0);

		vita2d_start_drawing();
		vita2d_clear_screen();
		if (mode == VR_MODE_GAMEPAD)
			screens_draw_gamepad(&pad, &rear);
		else if (mode == VR_MODE_MOUSE)
			screens_draw_mouse(&front, scroll_id >= 0);
		else
			kb_draw();
		screens_draw_header(mode);
		screens_draw_footer(mode);
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();
	}
	return 0;
}
