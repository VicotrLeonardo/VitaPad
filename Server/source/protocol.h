/*
 * Vita Remote - protocolo v2 (Vita <-> PC).
 * Espelhado em linux-client/vitaremote/protocol_v2.py. Mudou aqui, mude lá.
 *
 * Tudo em little-endian (Vita e x86 são little-endian).
 *
 *  Descoberta  UDP broadcast  Vita -> 255.255.255.255:5002  VrBeacon, 1x/s enquanto ninguém conectou
 *  Controle    TCP 5001       PC conecta; mensagens = [u8 tipo][u8 tamanho][payload]
 *  Estado      UDP            Vita:5003 -> PC (porta informada no HELLO), VrStatePacket em `hz` vezes/s
 *              O PC manda 1 datagrama qualquer para Vita:5003 a cada segundo: assim o firewall
 *              do PC trata o estado como resposta e deixa entrar sem regra extra.
 *
 *  Sequência:
 *    PC  -> HELLO(magic, versão, hz, porta UDP)
 *    Vita-> WELCOME(magic, versão, modo atual)   ou ERROR e fecha
 *    Vita-> estado por UDP continuamente
 *    PC  -> PING a cada 1 s; Vita -> PONG. 3 s sem nada do PC = Vita desconecta.
 *    Vita-> MODE quando o usuário troca de modo; KEY quando toca no teclado visual.
 */
#pragma once
#include <stdint.h>

#define VR_MAGIC            "VRMT"
#define VR_PROTOCOL_VERSION 2
#define VR_TCP_PORT         5001
#define VR_DISCOVERY_PORT   5002
#define VR_UDP_PORT         5003

enum VrMode {
	VR_MODE_GAMEPAD  = 0,
	VR_MODE_MOUSE    = 1,
	VR_MODE_KEYBOARD = 2,
	VR_MODE_COUNT
};

/* Tipos de mensagem TCP */
enum VrMsg {
	VR_MSG_HELLO   = 0x01, /* PC -> Vita: VrHello */
	VR_MSG_PING    = 0x02, /* PC -> Vita: sem payload */
	VR_MSG_WELCOME = 0x81, /* Vita -> PC: VrWelcome */
	VR_MSG_KEY     = 0x82, /* Vita -> PC: VrKey */
	VR_MSG_MODE    = 0x83, /* Vita -> PC: u8 modo */
	VR_MSG_PONG    = 0x84, /* Vita -> PC: sem payload */
	VR_MSG_ERROR   = 0x8F  /* Vita -> PC: u8 código (VR_ERR_*) */
};

enum VrError {
	VR_ERR_BAD_MAGIC   = 1,
	VR_ERR_BAD_VERSION = 2
};

#define VR_PACKED __attribute__((packed))

typedef struct VR_PACKED {
	char     magic[4];
	uint8_t  version;
	uint8_t  hz;        /* pacotes de estado por segundo pedidos pelo PC (30..250) */
	uint16_t udp_port;  /* porta UDP do PC que recebe o estado */
} VrHello;

typedef struct VR_PACKED {
	char    magic[4];
	uint8_t version;
	uint8_t mode;
} VrWelcome;

typedef struct VR_PACKED {
	uint16_t code;      /* código de tecla do Linux (KEY_*, input-event-codes.h) */
	uint8_t  pressed;   /* 1 = apertou, 0 = soltou */
} VrKey;

typedef struct VR_PACKED {
	uint16_t x;
	uint16_t y;
} VrPoint;

#define VR_UDP_STATE 1
#define VR_MAX_POINTS 2

/* 30 bytes. Coordenadas de toque em unidades do painel (frontal 0..1919 x 0..1087). */
typedef struct VR_PACKED {
	uint8_t  type;          /* VR_UDP_STATE */
	uint8_t  mode;          /* VrMode atual */
	uint16_t seq;           /* incrementa a cada pacote; PC descarta pacotes atrasados */
	uint32_t buttons;       /* bitmask SceCtrl */
	uint8_t  lx, ly, rx, ry;
	uint8_t  front_count;   /* dedos na tela frontal (0..2), fora da barra de abas */
	uint8_t  rear_count;    /* dedos no painel traseiro (0..2) */
	VrPoint  front[VR_MAX_POINTS];
	VrPoint  rear[VR_MAX_POINTS];
} VrStatePacket;

typedef struct VR_PACKED {
	char     magic[4];
	uint8_t  version;
	uint16_t tcp_port;
} VrBeacon;

_Static_assert(sizeof(VrHello) == 8, "VrHello");
_Static_assert(sizeof(VrWelcome) == 6, "VrWelcome");
_Static_assert(sizeof(VrKey) == 3, "VrKey");
_Static_assert(sizeof(VrStatePacket) == 30, "VrStatePacket");
_Static_assert(sizeof(VrBeacon) == 7, "VrBeacon");
