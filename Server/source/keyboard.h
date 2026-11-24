/* Teclado visual ABNT2. Toques viram teclas do Linux (KEY_*) enviadas ao PC. */
#pragma once
#include <psp2/ctrl.h>
#include <psp2/touch.h>

void kb_init(void);
/* Chamar a cada quadro no modo teclado: toques e botões (D-pad navega, ✕ aperta). */
void kb_update(const SceTouchData *front, const SceCtrlData *pad);
void kb_draw(void);
void kb_reset(void);                        /* solta teclas e modificadores (ao sair do modo) */
