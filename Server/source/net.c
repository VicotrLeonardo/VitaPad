/*
 * Rede do Vita Remote. Três threads:
 *   control_thread  aceita o PC por TCP, faz o handshake e responde PING;
 *                   se o PC some (3 s sem mensagens), volta a aceitar conexões.
 *   state_thread    lê botões/analógicos/toques e envia por UDP em `hz` vezes/s.
 *   beacon_thread   anuncia o Vita por broadcast enquanto ninguém está conectado.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/kernel/threadmgr.h>

#include "net.h"
#include "protocol.h"

#define NET_INIT_SIZE      (1 * 1024 * 1024)
#define PEER_TIMEOUT_US    (3 * 1000 * 1000)
#define THREAD_PRIORITY    0x10000100
#define THREAD_STACK       0x10000

static volatile int g_connected = 0;
static volatile int g_mode = VR_MODE_MOUSE;
static volatile uint8_t g_flags = 0;
static volatile int g_hz = 120;
static volatile uint32_t g_suppressed = 0;
static volatile unsigned g_sent = 0;
static int g_tcp = -1;                 /* protegido por g_send_mutex */
static SceUID g_send_mutex;
static SceNetSockaddrIn g_udp_dest;
static char g_local_ip[16] = "?";
static char g_peer_ip[16] = "";

const char *net_local_ip(void) { return g_local_ip; }
const char *net_peer_ip(void) { return g_peer_ip; }
int net_connected(void) { return g_connected; }
unsigned net_packets_sent(void) { return g_sent; }
int net_get_mode(void) { return g_mode; }
void net_set_suppressed_buttons(uint32_t mask) { g_suppressed = mask; }
void net_set_state_flags(uint8_t flags) { g_flags = flags; }

/* ------------------------------------------------------------------ TCP */

static int send_msg(uint8_t type, const void *payload, uint8_t len) {
	uint8_t buf[2 + 255];
	buf[0] = type;
	buf[1] = len;
	if (len)
		memcpy(buf + 2, payload, len);
	int ret = -1;
	sceKernelLockMutex(g_send_mutex, 1, NULL);
	if (g_tcp >= 0)
		ret = sceNetSend(g_tcp, buf, 2 + len, 0);
	sceKernelUnlockMutex(g_send_mutex, 1);
	return ret;
}

void net_set_mode(int mode) {
	if (mode < 0 || mode >= VR_MODE_COUNT || mode == g_mode)
		return;
	g_mode = mode;
	uint8_t m = (uint8_t)mode;
	if (g_connected)
		send_msg(VR_MSG_MODE, &m, 1);
}

void net_send_key(uint16_t code, int pressed) {
	if (!g_connected)
		return;
	VrKey k = { code, pressed ? 1 : 0 };
	send_msg(VR_MSG_KEY, &k, sizeof(k));
}

/* Lê exatamente `len` bytes. Erro, conexão fechada ou timeout => -1. */
static int recv_exact(int s, void *buf, int len) {
	int got = 0;
	while (got < len) {
		int r = sceNetRecv(s, (char *)buf + got, len - got, 0);
		if (r <= 0)
			return -1;
		got += r;
	}
	return got;
}

static void reject(int s, uint8_t code) {
	uint8_t msg[3] = { VR_MSG_ERROR, 1, code };
	sceNetSend(s, msg, sizeof(msg), 0);
}

static void serve_client(int s, const SceNetSockaddrIn *addr) {
	uint8_t hdr[2];
	uint8_t payload[256];

	if (recv_exact(s, hdr, 2) < 0 || hdr[0] != VR_MSG_HELLO || hdr[1] < sizeof(VrHello))
		return;
	if (recv_exact(s, payload, hdr[1]) < 0)
		return;

	VrHello hello;
	memcpy(&hello, payload, sizeof(hello));
	if (memcmp(hello.magic, VR_MAGIC, 4) != 0) {
		reject(s, VR_ERR_BAD_MAGIC);
		return;
	}
	if (hello.version != VR_PROTOCOL_VERSION) {
		reject(s, VR_ERR_BAD_VERSION);
		return;
	}

	int hz = hello.hz;
	g_hz = hz < 30 ? 30 : (hz > 250 ? 250 : hz);
	memset(&g_udp_dest, 0, sizeof(g_udp_dest));
	g_udp_dest.sin_family = SCE_NET_AF_INET;
	g_udp_dest.sin_addr = addr->sin_addr;
	g_udp_dest.sin_port = sceNetHtons(hello.udp_port);
	sceNetInetNtop(SCE_NET_AF_INET, &addr->sin_addr, g_peer_ip, sizeof(g_peer_ip));

	sceKernelLockMutex(g_send_mutex, 1, NULL);
	g_tcp = s;
	sceKernelUnlockMutex(g_send_mutex, 1);

	VrWelcome welcome;
	memcpy(welcome.magic, VR_MAGIC, 4);
	welcome.version = VR_PROTOCOL_VERSION;
	welcome.mode = (uint8_t)g_mode;
	if (send_msg(VR_MSG_WELCOME, &welcome, sizeof(welcome)) < 0)
		return;
	g_connected = 1;

	for (;;) {
		if (recv_exact(s, hdr, 2) < 0)
			break;
		if (hdr[1] && recv_exact(s, payload, hdr[1]) < 0)
			break;
		if (hdr[0] == VR_MSG_PING)
			send_msg(VR_MSG_PONG, NULL, 0);
	}
}

static int control_thread(SceSize args, void *argp) {
	int one = 1;
	int listen_fd = sceNetSocket("vr_listen", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
	sceNetSetsockopt(listen_fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR, &one, sizeof(one));

	SceNetSockaddrIn addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = SCE_NET_AF_INET;
	addr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
	addr.sin_port = sceNetHtons(VR_TCP_PORT);
	sceNetBind(listen_fd, (SceNetSockaddr *)&addr, sizeof(addr));
	sceNetListen(listen_fd, 1);

	for (;;) {
		SceNetSockaddrIn client_addr;
		unsigned int addrlen = sizeof(client_addr);
		int s = sceNetAccept(listen_fd, (SceNetSockaddr *)&client_addr, &addrlen);
		if (s < 0) {
			sceKernelDelayThread(100 * 1000);
			continue;
		}
		int timeout = PEER_TIMEOUT_US;
		sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &timeout, sizeof(timeout));
		sceNetSetsockopt(s, SCE_NET_IPPROTO_TCP, SCE_NET_TCP_NODELAY, &one, sizeof(one));

		serve_client(s, &client_addr);

		g_connected = 0;
		sceKernelLockMutex(g_send_mutex, 1, NULL);
		g_tcp = -1;
		sceKernelUnlockMutex(g_send_mutex, 1);
		g_peer_ip[0] = '\0';
		sceNetSocketClose(s);
	}
	return 0;
}

/* ------------------------------------------------------------------ UDP */

static int state_thread(SceSize args, void *argp) {
	int s = sceNetSocket("vr_state", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
	/* Porta fixa: o PC "abre" o firewall dele mandando datagramas para cá. */
	SceNetSockaddrIn local;
	memset(&local, 0, sizeof(local));
	local.sin_family = SCE_NET_AF_INET;
	local.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
	local.sin_port = sceNetHtons(VR_UDP_PORT);
	sceNetBind(s, (SceNetSockaddr *)&local, sizeof(local));
	char discard[16];
	VrStatePacket pkt;
	SceCtrlData pad;
	SceTouchData front, rear;
	uint16_t seq = 0;

	for (;;) {
		/* Esvazia os datagramas do PC (só servem para abrir o firewall). */
		while (sceNetRecv(s, discard, sizeof(discard), SCE_NET_MSG_DONTWAIT) > 0)
			;
		if (!g_connected) {
			sceKernelDelayThread(50 * 1000);
			continue;
		}
		sceCtrlPeekBufferPositive(0, &pad, 1);
		sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1);
		sceTouchPeek(SCE_TOUCH_PORT_BACK, &rear, 1);

		int mode = g_mode;
		memset(&pkt, 0, sizeof(pkt));
		pkt.type = VR_UDP_STATE;
		pkt.mode = (uint8_t)mode;
		pkt.seq = seq++;
		/* No modo teclado os botões são tratados no Vita (navegação no teclado visual). */
		pkt.buttons = mode == VR_MODE_KEYBOARD ? 0 : (pad.buttons & ~g_suppressed);
		pkt.flags = g_flags;
		pkt.lx = pad.lx;
		pkt.ly = pad.ly;
		pkt.rx = pad.rx;
		pkt.ry = pad.ry;

		/* No modo teclado a tela frontal é do teclado visual: não vai para o PC. */
		if (mode != VR_MODE_KEYBOARD) {
			for (unsigned i = 0; i < front.reportNum && pkt.front_count < VR_MAX_POINTS; i++) {
				if (front.report[i].y / 2 < UI_TAB_BAR_H)
					continue; /* toque na barra de abas */
				pkt.front[pkt.front_count].x = front.report[i].x;
				pkt.front[pkt.front_count].y = front.report[i].y;
				pkt.front_count++;
			}
		}
		for (unsigned i = 0; i < rear.reportNum && pkt.rear_count < VR_MAX_POINTS; i++) {
			pkt.rear[pkt.rear_count].x = rear.report[i].x;
			pkt.rear[pkt.rear_count].y = rear.report[i].y;
			pkt.rear_count++;
		}

		if (sceNetSendto(s, &pkt, sizeof(pkt), 0, (SceNetSockaddr *)&g_udp_dest, sizeof(g_udp_dest)) >= 0)
			g_sent++;
		sceKernelDelayThread(1000 * 1000 / g_hz);
	}
	return 0;
}

static int beacon_thread(SceSize args, void *argp) {
	int one = 1;
	int s = sceNetSocket("vr_beacon", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
	sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &one, sizeof(one));

	SceNetSockaddrIn dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = SCE_NET_AF_INET;
	dest.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_BROADCAST);
	dest.sin_port = sceNetHtons(VR_DISCOVERY_PORT);

	VrBeacon beacon;
	memcpy(beacon.magic, VR_MAGIC, 4);
	beacon.version = VR_PROTOCOL_VERSION;
	beacon.tcp_port = VR_TCP_PORT;

	for (;;) {
		if (!g_connected)
			sceNetSendto(s, &beacon, sizeof(beacon), 0, (SceNetSockaddr *)&dest, sizeof(dest));
		sceKernelDelayThread(1000 * 1000);
	}
	return 0;
}

/* ------------------------------------------------------------------ init */

static void start_thread(const char *name, SceKernelThreadEntry entry) {
	SceUID t = sceKernelCreateThread(name, entry, THREAD_PRIORITY, THREAD_STACK, 0, 0, NULL);
	if (t >= 0)
		sceKernelStartThread(t, 0, NULL);
}

int net_start(void) {
	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	if (sceNetShowNetstat() == (int)SCE_NET_ERROR_ENOTINIT) {
		SceNetInitParam param;
		param.memory = malloc(NET_INIT_SIZE);
		param.size = NET_INIT_SIZE;
		param.flags = 0;
		if (sceNetInit(&param) < 0)
			return -1;
	}
	sceNetCtlInit();
	SceNetCtlInfo info;
	if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) >= 0)
		snprintf(g_local_ip, sizeof(g_local_ip), "%s", info.ip_address);

	g_send_mutex = sceKernelCreateMutex("vr_send", 0, 0, NULL);
	start_thread("vr_control", control_thread);
	start_thread("vr_state", state_thread);
	start_thread("vr_beacon", beacon_thread);
	return 0;
}
