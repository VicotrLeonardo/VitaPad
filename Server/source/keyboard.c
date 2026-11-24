/*
 * Teclado visual no layout ABNT2.
 *
 * O Vita manda o *código da tecla física* (KEY_* do Linux), não o caractere: quem
 * transforma a tecla em "ç", "á" etc. é o layout ABNT2 configurado no PC. Por isso
 * as teclas mortas (´ ~ ^ ¨ `) funcionam como num teclado de verdade.
 *
 * Modificadores (Shift, Ctrl, Alt, AltGr), como no celular:
 *   1 toque  = vale para a próxima tecla
 *   2 toques = travado (fica pressionado até tocar de novo)
 * Segurar uma tecla mantém ela pressionada no PC, então a repetição automática funciona.
 *
 * Botões físicos (tratados aqui, não no PC, para a prévia sempre bater com o que foi enviado):
 *   D-pad = move o destaque entre as teclas    ✕ = aperta a tecla destacada
 *   ○ = Apagar   □ = Espaço   △ = Shift   START = Enter   L / R = seta esquerda / direita
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "keyboard.h"
#include "net.h"
#include "ui.h"

/* Códigos de tecla do Linux (linux/input-event-codes.h) */
enum {
	KEY_ESC = 1, KEY_1 = 2, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
	KEY_MINUS = 12, KEY_EQUAL = 13, KEY_BACKSPACE = 14, KEY_TAB = 15,
	KEY_Q = 16, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
	KEY_LEFTBRACE = 26, KEY_RIGHTBRACE = 27, KEY_ENTER = 28, KEY_LEFTCTRL = 29,
	KEY_A = 30, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
	KEY_SEMICOLON = 39, KEY_APOSTROPHE = 40, KEY_GRAVE = 41, KEY_LEFTSHIFT = 42, KEY_BACKSLASH = 43,
	KEY_Z = 44, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M,
	KEY_COMMA = 51, KEY_DOT = 52, KEY_SLASH = 53, KEY_LEFTALT = 56, KEY_SPACE = 57, KEY_CAPSLOCK = 58,
	KEY_F1 = 59, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10,
	KEY_102ND = 86, KEY_F11 = 87, KEY_F12 = 88, KEY_RO = 89, KEY_RIGHTALT = 100,
	KEY_UP = 103, KEY_LEFT = 105, KEY_RIGHT = 106, KEY_DOWN = 108, KEY_DELETE = 111
};

typedef enum { MOD_SHIFT, MOD_CTRL, MOD_ALT, MOD_ALTGR, MOD_COUNT } ModId;
static const uint16_t MOD_CODES[MOD_COUNT] = { KEY_LEFTSHIFT, KEY_LEFTCTRL, KEY_LEFTALT, KEY_RIGHTALT };
static const char *MOD_NAMES[MOD_COUNT] = { "Shift", "Ctrl", "Alt", "AltGr" };
enum { MOD_OFF, MOD_ONESHOT, MOD_LOCKED };

typedef enum { K_CHAR, K_ACTION, K_MOD, K_CAPS } Kind;
typedef enum { I_NONE, I_BKSP, I_ENTER, I_SHIFT, I_LEFT, I_UP, I_DOWN, I_RIGHT, I_SPACE } Icon;

typedef struct {
	const char *label;
	const char *shift;   /* caractere com Shift (NULL = mesma tecla em maiúscula, para letras) */
	uint16_t code;
	float w;             /* largura em unidades (15 unidades por linha) */
	uint8_t kind;
	int8_t mod;
	uint8_t icon;
	uint8_t row_h;       /* 0 = linha normal; 1 = linha das teclas de função (mais baixa) */
} KeyDef;

#define CH(l, s, c)   { l, s, c, 1.0f, K_CHAR, -1, I_NONE, 0 }
#define LT(l, c)      { l, NULL, c, 1.0f, K_CHAR, -1, I_NONE, 0 }
#define ACT(l, c, w)  { l, NULL, c, w, K_ACTION, -1, I_NONE, 0 }
#define ICO(c, w, i)  { NULL, NULL, c, w, K_ACTION, -1, i, 0 }
#define FN(l, c, w)   { l, NULL, c, w, K_ACTION, -1, I_NONE, 1 }
#define MOD(l, m, w, i) { l, NULL, 0, w, K_MOD, m, i, 0 }
#define END           { NULL, NULL, 0, 0, 0, 0, 0, 0 }

static const KeyDef ROW_FN[] = {
	FN("Esc", KEY_ESC, 1.5f), FN("F1", KEY_F1, 1), FN("F2", KEY_F2, 1), FN("F3", KEY_F3, 1), FN("F4", KEY_F4, 1),
	FN("F5", KEY_F5, 1), FN("F6", KEY_F6, 1), FN("F7", KEY_F7, 1), FN("F8", KEY_F8, 1), FN("F9", KEY_F9, 1),
	FN("F10", KEY_F10, 1), FN("F11", KEY_F11, 1), FN("F12", KEY_F12, 1), FN("Del", KEY_DELETE, 1.5f), END
};
static const KeyDef ROW_1[] = {
	CH("'", "\"", KEY_GRAVE), CH("1", "!", KEY_1), CH("2", "@", KEY_2), CH("3", "#", KEY_3), CH("4", "$", KEY_4),
	CH("5", "%", KEY_5), CH("6", "\xC2\xA8", KEY_6), CH("7", "&", KEY_7), CH("8", "*", KEY_8), CH("9", "(", KEY_9),
	CH("0", ")", KEY_0), CH("-", "_", KEY_MINUS), CH("=", "+", KEY_EQUAL), ICO(KEY_BACKSPACE, 2.0f, I_BKSP), END
};
static const KeyDef ROW_2[] = {
	ACT("Tab", KEY_TAB, 1.5f), LT("q", KEY_Q), LT("w", KEY_W), LT("e", KEY_E), LT("r", KEY_R), LT("t", KEY_T),
	LT("y", KEY_Y), LT("u", KEY_U), LT("i", KEY_I), LT("o", KEY_O), LT("p", KEY_P),
	CH("\xC2\xB4", "`", KEY_LEFTBRACE), CH("[", "{", KEY_RIGHTBRACE), ICO(KEY_ENTER, 1.5f, I_ENTER), END
};
static const KeyDef ROW_3[] = {
	{ "Caps", NULL, KEY_CAPSLOCK, 1.75f, K_CAPS, -1, I_NONE, 0 },
	LT("a", KEY_A), LT("s", KEY_S), LT("d", KEY_D), LT("f", KEY_F), LT("g", KEY_G), LT("h", KEY_H),
	LT("j", KEY_J), LT("k", KEY_K), LT("l", KEY_L), LT("\xC3\xA7", KEY_SEMICOLON),
	CH("~", "^", KEY_APOSTROPHE), CH("]", "}", KEY_BACKSLASH), ICO(KEY_ENTER, 1.25f, I_ENTER), END
};
static const KeyDef ROW_4[] = {
	MOD("Shift", MOD_SHIFT, 1.25f, I_SHIFT), CH("\\", "|", KEY_102ND),
	LT("z", KEY_Z), LT("x", KEY_X), LT("c", KEY_C), LT("v", KEY_V), LT("b", KEY_B), LT("n", KEY_N), LT("m", KEY_M),
	CH(",", "<", KEY_COMMA), CH(".", ">", KEY_DOT), CH(";", ":", KEY_SLASH), CH("/", "?", KEY_RO),
	MOD("Shift", MOD_SHIFT, 1.75f, I_SHIFT), END
};
static const KeyDef ROW_5[] = {
	MOD("Ctrl", MOD_CTRL, 1.5f, I_NONE), MOD("Alt", MOD_ALT, 1.5f, I_NONE), ICO(KEY_SPACE, 6.0f, I_SPACE),
	MOD("AltGr", MOD_ALTGR, 1.5f, I_NONE), ICO(KEY_LEFT, 1.125f, I_LEFT), ICO(KEY_UP, 1.125f, I_UP),
	ICO(KEY_DOWN, 1.125f, I_DOWN), ICO(KEY_RIGHT, 1.125f, I_RIGHT), END
};
static const KeyDef *const ROWS[] = { ROW_FN, ROW_1, ROW_2, ROW_3, ROW_4, ROW_5 };
#define NUM_ROWS (int)(sizeof(ROWS) / sizeof(ROWS[0]))

/* Geometria */
#define KB_X      20
#define KB_W      (SCREEN_W - 40)
#define KB_GAP    6
#define ROW_H     54
#define FN_ROW_H  38
#define ECHO_Y    (BODY_Y + 10)
#define ECHO_H    52
#define KB_Y      (ECHO_Y + ECHO_H + 12)
#define MAX_KEYS  96
#define MAX_FINGERS 6

typedef struct {
	const KeyDef *def;
	int x, y, w, h;
	int enter_part; /* 0 = não é Enter; 1 = parte de cima; 2 = parte de baixo */
} Key;

static Key keys[MAX_KEYS];
static int num_keys;
static uint8_t held[MAX_KEYS];           /* quantos dedos seguram a tecla */

static uint8_t mod_state[MOD_COUNT];
static uint8_t mod_sent[MOD_COUNT];      /* modificador de 1 toque já foi enviado ao PC */
static int caps_on;

typedef struct { int active; uint8_t id; int key; } Finger;
static Finger fingers[MAX_FINGERS];

/* Navegação pelos botões */
static int focus = -1;
static int focus_visible;
static uint32_t prev_buttons;
static int resync_buttons = 1;
static int dpad_dir = -1;
static uint64_t dpad_next;
static int start_with_select;

typedef struct { uint32_t button; int key; } ButtonHold;
static ButtonHold holds[8];

static char echo[160];
static char toast[32];
static uint64_t toast_until;

/* ------------------------------------------------------------ init */

void kb_init(void) {
	float unit = (KB_W + KB_GAP) / 15.0f;
	int y = KB_Y;
	num_keys = 0;
	for (int r = 0; r < NUM_ROWS; r++) {
		int h = ROWS[r][0].row_h ? FN_ROW_H : ROW_H;
		float u = 0;
		for (const KeyDef *d = ROWS[r]; d->w > 0 && num_keys < MAX_KEYS; d++) {
			Key *k = &keys[num_keys++];
			k->def = d;
			k->x = KB_X + (int)(u * unit);
			k->w = (int)((u + d->w) * unit) - (int)(u * unit) - KB_GAP;
			k->y = y;
			k->h = h;
			k->enter_part = d->icon == I_ENTER ? (ROWS[r] == ROW_2 ? 1 : 2) : 0;
			u += d->w;
		}
		y += h + KB_GAP;
	}
}

/* ------------------------------------------------------------ envio */

static void show_toast(const char *s) {
	snprintf(toast, sizeof(toast), "%s", s);
	toast_until = ui_time_us() + 1200 * 1000;
}

static int shift_active(void) { return mod_state[MOD_SHIFT] != MOD_OFF; }

static int is_letter(const KeyDef *d) { return d->kind == K_CHAR && d->shift == NULL; }

static void echo_backspace(void) {
	int n = strlen(echo);
	if (n == 0)
		return;
	n--;
	while (n > 0 && ((unsigned char)echo[n] & 0xC0) == 0x80) /* volta até o início do caractere UTF-8 */
		n--;
	echo[n] = '\0';
}

static void echo_append(const char *s) {
	if (strlen(echo) + strlen(s) >= sizeof(echo) - 1)
		memmove(echo, echo + 32, strlen(echo + 32) + 1); /* descarta o começo */
	strcat(echo, s);
}

static void letter_text(const KeyDef *d, int upper, char *out, int size) {
	if (strcmp(d->label, "\xC3\xA7") == 0) {
		snprintf(out, size, "%s", upper ? "\xC3\x87" : "\xC3\xA7");
		return;
	}
	out[0] = upper ? (char)toupper((unsigned char)d->label[0]) : d->label[0];
	out[1] = '\0';
}

/* Atualiza a prévia local e o aviso de atalho. */
static void describe(const KeyDef *d) {
	int shortcut = mod_state[MOD_CTRL] || mod_state[MOD_ALT];
	char buf[32];
	if (d->kind == K_CHAR && !shortcut && !mod_state[MOD_ALTGR]) {
		if (is_letter(d))
			letter_text(d, shift_active() ^ caps_on, buf, sizeof(buf));
		else
			snprintf(buf, sizeof(buf), "%s", shift_active() ? d->shift : d->label);
		echo_append(buf);
		return;
	}
	if (d->code == KEY_BACKSPACE && !shortcut) { echo_backspace(); return; }
	if (d->code == KEY_SPACE && !shortcut)     { echo_append(" "); return; }
	if (d->code == KEY_ENTER && !shortcut)     { echo[0] = '\0'; show_toast("Enter"); return; }

	/* Atalhos e teclas de ação: mostra o nome em vez de "digitar". */
	char name[32] = "";
	for (int m = 0; m < MOD_COUNT; m++) {
		if (mod_state[m]) {
			strcat(name, MOD_NAMES[m]);
			strcat(name, " + ");
		}
	}
	const char *key_name = d->label;
	char letter[8];
	switch (d->icon) {
	case I_BKSP:  key_name = "Apagar"; break;
	case I_ENTER: key_name = "Enter"; break;
	case I_SPACE: key_name = "Espaço"; break;
	case I_LEFT:  key_name = "Esquerda"; break;
	case I_RIGHT: key_name = "Direita"; break;
	case I_UP:    key_name = "Cima"; break;
	case I_DOWN:  key_name = "Baixo"; break;
	default:
		if (is_letter(d)) {
			letter_text(d, 1, letter, sizeof(letter));
			key_name = letter;
		}
	}
	if (strlen(name) + strlen(key_name) < sizeof(name))
		strcat(name, key_name);
	show_toast(name);
}

static void press_key(int i) {
	const KeyDef *d = keys[i].def;
	held[i]++;
	if (held[i] > 1)
		return;

	if (d->kind == K_MOD) {
		int m = d->mod;
		if (mod_state[m] == MOD_OFF) {
			mod_state[m] = MOD_ONESHOT;
		} else if (mod_state[m] == MOD_ONESHOT) {
			mod_state[m] = MOD_LOCKED;
			if (!mod_sent[m])
				net_send_key(MOD_CODES[m], 1);
			mod_sent[m] = 1;
		} else {
			mod_state[m] = MOD_OFF;
			if (mod_sent[m])
				net_send_key(MOD_CODES[m], 0);
			mod_sent[m] = 0;
		}
		return;
	}
	if (d->kind == K_CAPS) {
		caps_on = !caps_on;
		net_send_key(KEY_CAPSLOCK, 1);
		show_toast(caps_on ? "Caps Lock ligado" : "Caps Lock desligado");
		return;
	}
	for (int m = 0; m < MOD_COUNT; m++) {
		if (mod_state[m] == MOD_ONESHOT && !mod_sent[m]) {
			net_send_key(MOD_CODES[m], 1);
			mod_sent[m] = 1;
		}
	}
	describe(d);
	net_send_key(d->code, 1);
}

static void release_key(int i) {
	const KeyDef *d = keys[i].def;
	if (held[i] == 0)
		return;
	held[i]--;
	if (held[i] > 0)
		return;
	if (d->kind == K_MOD)
		return;
	if (d->kind == K_CAPS) {
		net_send_key(KEY_CAPSLOCK, 0);
		return;
	}
	net_send_key(d->code, 0);
	for (int m = 0; m < MOD_COUNT; m++) {
		if (mod_state[m] == MOD_ONESHOT) {
			if (mod_sent[m])
				net_send_key(MOD_CODES[m], 0);
			mod_state[m] = MOD_OFF;
			mod_sent[m] = 0;
		}
	}
}

void kb_reset(void) {
	resync_buttons = 1;
	for (int b = 0; b < 8; b++)
		holds[b].button = 0;
	for (int f = 0; f < MAX_FINGERS; f++)
		fingers[f].active = 0;
	for (int i = 0; i < num_keys; i++) {
		if (held[i]) {
			held[i] = 1;
			release_key(i);
		}
	}
	for (int m = 0; m < MOD_COUNT; m++) {
		if (mod_sent[m])
			net_send_key(MOD_CODES[m], 0);
		mod_state[m] = MOD_OFF;
		mod_sent[m] = 0;
	}
}

/* ------------------------------------------------------------ toque */

static int key_at(int x, int y) {
	for (int i = 0; i < num_keys; i++) {
		Key *k = &keys[i];
		int top = k->y, bottom = k->y + k->h;
		if (k->enter_part == 2)
			top -= KB_GAP; /* cobre o vão entre as duas partes do Enter */
		if (x >= k->x && x < k->x + k->w && y >= top && y < bottom)
			return i;
	}
	return -1;
}

static void update_buttons(const SceCtrlData *pad);

void kb_update(const SceTouchData *front, const SceCtrlData *pad) {
	update_buttons(pad);
	/* Dedos que saíram da tela soltam a tecla em que começaram. */
	for (int f = 0; f < MAX_FINGERS; f++) {
		if (!fingers[f].active)
			continue;
		int still = 0;
		for (unsigned r = 0; r < front->reportNum; r++)
			if (front->report[r].id == fingers[f].id)
				still = 1;
		if (!still) {
			fingers[f].active = 0;
			if (fingers[f].key >= 0)
				release_key(fingers[f].key);
		}
	}
	/* Dedos novos apertam a tecla embaixo deles. */
	for (unsigned r = 0; r < front->reportNum; r++) {
		const SceTouchReport *rep = &front->report[r];
		int known = 0;
		for (int f = 0; f < MAX_FINGERS; f++)
			if (fingers[f].active && fingers[f].id == rep->id)
				known = 1;
		if (known)
			continue;
		for (int f = 0; f < MAX_FINGERS; f++) {
			if (!fingers[f].active) {
				fingers[f].active = 1;
				fingers[f].id = rep->id;
				fingers[f].key = key_at(rep->x / 2, rep->y / 2);
				if (fingers[f].key >= 0) {
					press_key(fingers[f].key);
					focus = fingers[f].key;  /* o destaque segue o último toque, mas some */
					focus_visible = 0;
				}
				break;
			}
		}
	}
}


/* ------------------------------------------------------------ botões */

static int find_key(uint16_t code, int mod) {
	for (int i = 0; i < num_keys; i++) {
		const KeyDef *d = keys[i].def;
		if (mod >= 0 ? (d->kind == K_MOD && d->mod == mod) : (d->code == code && d->kind != K_MOD))
			return i;
	}
	return -1;
}

static int center_x(int i) { return keys[i].x + keys[i].w / 2; }

/* Move o destaque. dir: 0 cima, 1 direita, 2 baixo, 3 esquerda. */
static void move_focus(int dir) {
	if (focus < 0 || !focus_visible) {
		if (focus < 0)
			focus = find_key(KEY_G, -1);
		focus_visible = 1;
		return;
	}
	const Key *cur = &keys[focus];
	int best = -1, best_score = 1 << 30;
	if (dir == 1 || dir == 3) {
		int wrap = -1, wrap_x = dir == 1 ? 1 << 30 : -1;
		for (int i = 0; i < num_keys; i++) {
			if (keys[i].y != cur->y || i == focus)
				continue;
			int d = center_x(i) - center_x(focus);
			if ((dir == 1 && d > 0) || (dir == 3 && d < 0)) {
				int score = d < 0 ? -d : d;
				if (score < best_score) { best_score = score; best = i; }
			}
			/* Para dar a volta na linha: a tecla mais à esquerda (indo p/ direita) ou mais à direita. */
			int cx = center_x(i);
			if ((dir == 1 && cx < wrap_x) || (dir == 3 && cx > wrap_x)) { wrap_x = cx; wrap = i; }
		}
		if (best < 0)
			best = wrap;
	} else {
		/* Acha a linha vizinha (y imediatamente acima ou abaixo). */
		int target_y = -1;
		for (int i = 0; i < num_keys; i++) {
			int y = keys[i].y;
			if (dir == 2 && y > cur->y && (target_y < 0 || y < target_y)) target_y = y;
			if (dir == 0 && y < cur->y && (target_y < 0 || y > target_y)) target_y = y;
		}
		if (target_y < 0)
			return;
		for (int i = 0; i < num_keys; i++) {
			if (keys[i].y != target_y)
				continue;
			int d = center_x(i) - center_x(focus);
			int score = d < 0 ? -d : d;
			if (score < best_score) { best_score = score; best = i; }
		}
	}
	if (best >= 0)
		focus = best;
}

static void hold_start(uint32_t button, int key) {
	if (key < 0)
		return;
	for (int b = 0; b < 8; b++) {
		if (holds[b].button == 0) {
			holds[b].button = button;
			holds[b].key = key;
			press_key(key);
			return;
		}
	}
}

static void hold_end(uint32_t button) {
	for (int b = 0; b < 8; b++) {
		if (holds[b].button == button) {
			holds[b].button = 0;
			release_key(holds[b].key);
		}
	}
}

static void update_buttons(const SceCtrlData *pad) {
	uint32_t now_b = pad->buttons;
	if (resync_buttons) { /* entrou no modo: ignora botões que já estavam apertados */
		prev_buttons = now_b;
		resync_buttons = 0;
		dpad_dir = -1;
		return;
	}
	uint32_t down = now_b & ~prev_buttons, up = prev_buttons & ~now_b;

	/* D-pad com repetição ao segurar */
	static const uint32_t DIRS[4] = { SCE_CTRL_UP, SCE_CTRL_RIGHT, SCE_CTRL_DOWN, SCE_CTRL_LEFT };
	uint64_t t = ui_time_us();
	for (int d = 0; d < 4; d++) {
		if (down & DIRS[d]) {
			move_focus(d);
			dpad_dir = d;
			dpad_next = t + 380 * 1000;
		}
	}
	if (dpad_dir >= 0) {
		if (!(now_b & DIRS[dpad_dir]))
			dpad_dir = -1;
		else if (t >= dpad_next) {
			move_focus(dpad_dir);
			dpad_next = t + 90 * 1000;
		}
	}

	/* ✕ aperta a tecla destacada (o primeiro ✕ só mostra o destaque). */
	if (down & SCE_CTRL_CROSS) {
		if (focus < 0 || !focus_visible)
			move_focus(-1);
		else
			hold_start(SCE_CTRL_CROSS, focus);
	}
	if (down & SCE_CTRL_CIRCLE)   hold_start(SCE_CTRL_CIRCLE, find_key(KEY_BACKSPACE, -1));
	if (down & SCE_CTRL_SQUARE)   hold_start(SCE_CTRL_SQUARE, find_key(KEY_SPACE, -1));
	if (down & SCE_CTRL_TRIANGLE) hold_start(SCE_CTRL_TRIANGLE, find_key(0, MOD_SHIFT));
	if (down & SCE_CTRL_LTRIGGER) hold_start(SCE_CTRL_LTRIGGER, find_key(KEY_LEFT, -1));
	if (down & SCE_CTRL_RTRIGGER) hold_start(SCE_CTRL_RTRIGGER, find_key(KEY_RIGHT, -1));
	for (int b = 0; b < 6; b++) {
		static const uint32_t BTN[6] = { SCE_CTRL_CROSS, SCE_CTRL_CIRCLE, SCE_CTRL_SQUARE,
		                                 SCE_CTRL_TRIANGLE, SCE_CTRL_LTRIGGER, SCE_CTRL_RTRIGGER };
		if (up & BTN[b])
			hold_end(BTN[b]);
	}

	/* START = Enter, no soltar, e só se não fez parte de SELECT+START (troca de modo). */
	if (down & SCE_CTRL_START)
		start_with_select = (now_b & SCE_CTRL_SELECT) != 0;
	if ((now_b & SCE_CTRL_START) && (now_b & SCE_CTRL_SELECT))
		start_with_select = 1;
	if ((up & SCE_CTRL_START) && !start_with_select) {
		int enter = find_key(KEY_ENTER, -1);
		if (enter >= 0) {
			press_key(enter);
			release_key(enter);
		}
	}
	prev_buttons = now_b;
}

/* ------------------------------------------------------------ desenho */

static void draw_icon(const Key *k, int cx, int cy, uint32_t color) {
	switch (k->def->icon) {
	case I_BKSP:
		ui_arrow(cx - 14, cy, 16, 3, color);
		ui_line(cx - 8, cy, cx + 16, cy, 4, color);
		break;
	case I_ENTER:
		ui_line(cx + 14, cy - 12, cx + 14, cy + 4, 4, color);
		ui_line(cx + 16, cy + 4, cx - 8, cy + 4, 4, color);
		ui_arrow(cx - 12, cy + 4, 14, 3, color);
		break;
	case I_SHIFT:
		ui_arrow(cx, cy - 4, 20, 0, color);
		vita2d_draw_rectangle(cx - 5, cy + 4, 10, 8, color);
		break;
	case I_LEFT:  ui_arrow(cx, cy, 16, 3, color); break;
	case I_RIGHT: ui_arrow(cx, cy, 16, 1, color); break;
	case I_UP:    ui_arrow(cx, cy, 16, 0, color); break;
	case I_DOWN:  ui_arrow(cx, cy, 16, 2, color); break;
	case I_SPACE:
		ui_text_center(cx, cy, C_FAINT, T_SMALL, "espaço");
		break;
	}
}

static void draw_key(int i) {
	const Key *k = &keys[i];
	const KeyDef *d = k->def;
	int pressed = held[i] > 0;
	if (k->enter_part) /* as duas partes do Enter acendem juntas */
		for (int j = 0; j < num_keys; j++)
			if (keys[j].enter_part && held[j])
				pressed = 1;
	int mod = d->kind == K_MOD ? mod_state[d->mod] : MOD_OFF;

	uint32_t fill = (d->kind == K_CHAR) ? C_SURFACE2 : C_SURFACE3;
	uint32_t fg = C_TEXT;
	if (mod == MOD_LOCKED || pressed) {
		fill = C_ACCENT;
	} else if (mod == MOD_ONESHOT) {
		fill = RGBA8(0x27, 0x3A, 0x5C, 0xFF);
		fg = RGBA8(0xB8, 0xD0, 0xFF, 0xFF);
	}

	ui_rrect(k->x, k->y, k->w, k->h, 8, fill);
	if (k->enter_part == 2) /* liga a parte de baixo do Enter à de cima */
		vita2d_draw_rectangle(k->x, k->y - KB_GAP - 8, k->w, KB_GAP + 16, fill);
	if (k->enter_part == 1)
		return; /* rótulo vai na parte de baixo, que fica centralizada no "L" */

	int cx = k->x + k->w / 2, cy = k->y + k->h / 2;
	if (k->enter_part == 2)
		cy = k->y - KB_GAP / 2;

	if (d->icon != I_NONE) {
		draw_icon(k, cx, cy, fg);
		return;
	}
	if (d->kind == K_CHAR && is_letter(d)) {
		char buf[8];
		letter_text(d, shift_active() ^ caps_on, buf, sizeof(buf));
		ui_text_center(cx, cy, fg, T_LABEL, buf);
		return;
	}
	if (d->kind == K_CHAR) {
		const char *main_label = shift_active() ? d->shift : d->label;
		const char *corner = shift_active() ? d->label : d->shift;
		ui_text_center(cx, cy + 2, fg, T_LABEL, main_label);
		ui_text(k->x + 6, k->y + 16, C_FAINT, T_SMALL * 0.85f, corner);
		return;
	}
	ui_text_center(cx, cy, fg, d->row_h ? T_SMALL : T_BODY * 0.9f, d->label);
	if (d->kind == K_CAPS)
		vita2d_draw_fill_circle(k->x + k->w - 10, k->y + 10, 4, caps_on ? C_OK : C_FAINT);
	if (mod == MOD_LOCKED)
		ui_rrect(cx - 8, k->y + k->h - 9, 16, 3, 1, C_TEXT);
}

static void draw_echo(void) {
	int x = KB_X, w = KB_W;
	ui_rrect(x, ECHO_Y, w, ECHO_H, 12, C_SURFACE);
	ui_text_mid(x + 16, ECHO_Y + 15, C_FAINT, T_SMALL * 0.85f, "PRÉVIA LOCAL");

	/* Mostra só o final do texto que cabe. */
	const char *s = echo;
	int max_w = w - 40 - 200;
	while (*s && ui_text_w(T_LABEL, s) > max_w) {
		s++;
		while (((unsigned char)*s & 0xC0) == 0x80)
			s++;
	}
	int tx = x + 16, ty = ECHO_Y + 35;
	if (*s)
		ui_text_mid(tx, ty, C_TEXT, T_LABEL, s);
	else if (!net_connected())
		ui_text_mid(tx, ty, C_FAINT, T_BODY, "Conecte o PC para digitar");
	else
		ui_text_mid(tx, ty, C_FAINT, T_BODY, "Toque nas teclas para digitar no PC");
	int caret_x = tx + (*s ? ui_text_w(T_LABEL, s) + 3 : 0);
	if ((ui_time_us() / 500000) % 2 == 0 && *s)
		vita2d_draw_rectangle(caret_x, ty - 11, 2, 22, C_ACCENT);

	if (toast[0] && ui_time_us() < toast_until) {
		int tw = ui_text_w(T_BODY, toast) + 28;
		ui_rrect(x + w - tw - 10, ECHO_Y + 10, tw, ECHO_H - 20, 16, C_ACCENT_DIM);
		ui_text_center(x + w - 10 - tw / 2, ECHO_Y + ECHO_H / 2, C_TEXT, T_BODY, toast);
	}
}

static void draw_focus(void) {
	if (focus < 0 || !focus_visible)
		return;
	for (int i = 0; i < num_keys; i++) {
		const Key *k = &keys[i];
		/* As duas partes do Enter ganham o contorno juntas. */
		if (!(i == focus || (k->enter_part && keys[focus].enter_part)))
			continue;
		float x = k->x - 3, y = k->y - 3, w = k->w + 6, h = k->h + 6;
		uint32_t c = C_TEXT;
		ui_line(x, y, x + w, y, 3, c);
		ui_line(x, y + h, x + w, y + h, 3, c);
		ui_line(x, y - 1.5f, x, y + h + 1.5f, 3, c);
		ui_line(x + w, y - 1.5f, x + w, y + h + 1.5f, 3, c);
	}
}

void kb_draw(void) {
	draw_echo();
	for (int i = 0; i < num_keys; i++)
		draw_key(i);
	draw_focus();
}
