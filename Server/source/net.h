#pragma once
#include <stdint.h>

/* Altura da barra de abas, em pixels de tela (960x544). Toques nela não vão para o PC. */
#define UI_TAB_BAR_H 64

/* Inicializa a rede e inicia as threads de controle (TCP), estado (UDP) e descoberta. */
int net_start(void);

const char *net_local_ip(void);
const char *net_peer_ip(void);
int net_connected(void);
unsigned net_packets_sent(void);

int  net_get_mode(void);
void net_set_mode(int mode);      /* troca o modo e avisa o PC */

/* Botões que não devem ser enviados (ex.: SELECT+START enquanto trocam de modo). */
void net_set_suppressed_buttons(uint32_t mask);

/* Envia uma tecla do teclado visual (código KEY_* do Linux). */
void net_send_key(uint16_t code, int pressed);
