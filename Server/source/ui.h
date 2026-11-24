/* Base visual do Vita Remote: tema, texto e formas. */
#pragma once
#include <stdint.h>
#include <vita2d.h>

#define SCREEN_W 960
#define SCREEN_H 544
#define HEADER_H 60
#define FOOTER_H 38
#define BODY_Y   HEADER_H
#define BODY_H   (SCREEN_H - HEADER_H - FOOTER_H)

/* Paleta (RGBA8 do vita2d é r, g, b, a) */
#define C_BG        RGBA8(0x0D, 0x10, 0x16, 0xFF)
#define C_SURFACE   RGBA8(0x16, 0x1A, 0x23, 0xFF)
#define C_SURFACE2  RGBA8(0x20, 0x26, 0x32, 0xFF)
#define C_SURFACE3  RGBA8(0x2B, 0x32, 0x41, 0xFF)
#define C_BORDER    RGBA8(0x30, 0x38, 0x48, 0xFF)
#define C_TEXT      RGBA8(0xEC, 0xEF, 0xF4, 0xFF)
#define C_MUTED     RGBA8(0x8C, 0x95, 0xA8, 0xFF)
#define C_FAINT     RGBA8(0x55, 0x5E, 0x70, 0xFF)
#define C_ACCENT    RGBA8(0x4C, 0x8D, 0xFF, 0xFF)
#define C_ACCENT_DIM RGBA8(0x4C, 0x8D, 0xFF, 0x40)
#define C_OK        RGBA8(0x3D, 0xD6, 0x8C, 0xFF)
#define C_WARN      RGBA8(0xF5, 0xB8, 0x4A, 0xFF)

/* Cores dos símbolos PlayStation */
#define C_TRIANGLE  RGBA8(0x3E, 0xD6, 0xC0, 0xFF)
#define C_CIRCLE    RGBA8(0xFF, 0x6B, 0x7A, 0xFF)
#define C_CROSS     RGBA8(0x7C, 0xA9, 0xFF, 0xFF)
#define C_SQUARE    RGBA8(0xE5, 0x8F, 0xD9, 0xFF)

/* Tamanhos de texto (escala da fonte PVF do sistema) */
#define T_SMALL  0.75f
#define T_BODY   0.9f
#define T_LABEL  1.0f
#define T_TITLE  1.25f

typedef enum {
	GLYPH_TRIANGLE,
	GLYPH_CIRCLE,
	GLYPH_CROSS,
	GLYPH_SQUARE
} Glyph;

void ui_init(void);
uint64_t ui_time_us(void);

/* Texto: y é a linha de base; as variantes _mid centralizam verticalmente em cy. */
int  ui_text_w(float scale, const char *s);
void ui_text(int x, int y, uint32_t color, float scale, const char *s);
void ui_text_mid(int x, int cy, uint32_t color, float scale, const char *s);
void ui_text_center(int cx, int cy, uint32_t color, float scale, const char *s);
void ui_text_right(int right, int cy, uint32_t color, float scale, const char *s);

/* Formas */
void ui_rrect(float x, float y, float w, float h, float r, uint32_t color);
void ui_rrect_border(float x, float y, float w, float h, float r, float border, uint32_t fill, uint32_t line);
void ui_line(float x0, float y0, float x1, float y1, float width, uint32_t color);
void ui_ring(float cx, float cy, float radius, float width, uint32_t color);
void ui_triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color);

/* Símbolo PlayStation desenhado com formas (a fonte do sistema não tem os glifos). */
void ui_glyph(Glyph g, float cx, float cy, float size, uint32_t color);
uint32_t ui_glyph_color(Glyph g);

/* Setas e ícones simples */
void ui_arrow(float cx, float cy, float size, int dir, uint32_t color); /* dir: 0 cima 1 direita 2 baixo 3 esquerda */

/* "Chip" de legenda do rodapé: [símbolo] Texto. Devolve a largura usada. */
int ui_legend_glyph(int x, int cy, Glyph g, const char *label);
int ui_legend_key(int x, int cy, const char *key, const char *label);

uint32_t ui_alpha(uint32_t color, uint8_t alpha);
