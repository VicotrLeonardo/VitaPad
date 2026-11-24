#pragma once
#include <psp2/ctrl.h>
#include <psp2/touch.h>

/* Abas do cabeçalho (área de toque = altura inteira do cabeçalho). */
#define TABS_X  250
#define TABS_W  460
#define TAB_COUNT 3

/* Faixa de rolagem no meio da aba Mouse (pixels de tela). */
#define SCROLL_ZONE_W  120
#define SCROLL_ZONE_X  ((960 - SCROLL_ZONE_W) / 2)

/* Devolve o modo da aba em (x, y) de tela, ou -1. */
int screens_tab_at(int x, int y);

/* Próximo modo na ordem das abas (Mouse, Teclado, Gamepad). */
int screens_next_mode(int mode);

/* O ponto (x, y) de tela está na faixa de rolagem? */
int screens_in_scroll_zone(int x, int y);

void screens_draw_header(int mode);
void screens_draw_footer(int mode);
void screens_draw_gamepad(const SceCtrlData *pad, const SceTouchData *rear);
void screens_draw_mouse(const SceTouchData *front, int scrolling);
