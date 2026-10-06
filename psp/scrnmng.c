/**
 * @file	scrnmng.c
 * @brief	PSP 用スクリーンマネージャ
 *
 * X1 の 640x400 出力を自前の 16bpp (RGB565) サーフェースに描き、
 * SDL_BlitScaled でウィンドウサーフェース (480x272) に縮小転送する。
 *
 * 注意: PSP の GE はテクスチャ 512x512 までなので、640x400 のテクスチャを
 * SDL_Renderer で直接扱うことはできない。ウィンドウサーフェース経由だと
 * SDL 内部のフォールバックが 480x272 のストリーミングテクスチャを使うため
 * この制限にかからない。同一ウィンドウで Renderer API を併用しないこと。
 *
 * メニュー (menubase) 連携は未実装。sysmenu がスタブでメニューを開かない
 * ため scrnmng_entermenu/menudraw は呼ばれない。menuvram へ参照を持たない
 * ので、embed/menubase をリンクしない構成でもリンク可能。
 */

#include	"compiler.h"
#include	<SDL.h>
#include	"xmil.h"
#include	"scrnmng.h"
#include	"scrndraw.h"
#include	"vramhdl.h"

static SDL_Window	*s_sdlWindow;
static SDL_Surface	*s_surface;		/* 640x400 RGB565 作業サーフェース */

typedef struct {
	BOOL		enable;
	int			width;
	int			height;
	int			bpp;
	SDL_Surface	*surface;
} SCRNMNG;

typedef struct {
	int		width;
	int		height;
} SCRNSTAT;

static const char app_name[] = "X millennium";

static	SCRNMNG		scrnmng;
static	SCRNSTAT	scrnstat;
static	SCRNSURF	scrnsurf;

/* vram/palettes.c が参照する。0 = スキャンライン表示なし */
int allow_scanlines = 0;

void scrnmng_initialize(void) {

	scrnstat.width = 640;
	scrnstat.height = 400;
}

BOOL scrnmng_create(int width, int height) {

	SDL_PixelFormat	*fmt;

	if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
		fprintf(stderr, "Error: SDL_Init: %s\n", SDL_GetError());
		return(FAILURE);
	}
	s_sdlWindow = SDL_CreateWindow(app_name,
							SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
							PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT,
							SDL_WINDOW_SHOWN);
	if (s_sdlWindow == NULL) {
		fprintf(stderr, "Error: SDL_CreateWindow: %s\n", SDL_GetError());
		return(FAILURE);
	}
	s_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 16,
										0xf800, 0x07e0, 0x001f, 0);
	if (s_surface == NULL) {
		fprintf(stderr, "Error: SDL_CreateRGBSurface: %s\n", SDL_GetError());
		return(FAILURE);
	}

	fmt = s_surface->format;
	if ((fmt->BitsPerPixel != 16) || (fmt->Rmask != 0xf800) ||
		(fmt->Gmask != 0x07e0) || (fmt->Bmask != 0x001f)) {
		fprintf(stderr, "Error: Bad screen mode\n");
		return(FAILURE);
	}
	scrnmng.enable = TRUE;
	scrnmng.width = width;
	scrnmng.height = height;
	scrnmng.bpp = fmt->BitsPerPixel;
	return(SUCCESS);
}

void scrnmng_destroy(void) {

	scrnmng.enable = FALSE;
	if (s_surface) {
		SDL_FreeSurface(s_surface);
		s_surface = NULL;
	}
	if (s_sdlWindow) {
		SDL_DestroyWindow(s_sdlWindow);
		s_sdlWindow = NULL;
	}
}

RGB16 scrnmng_makepal16(RGB32 pal32) {

	RGB16	ret;

	ret = (pal32.p.r & 0xf8) << 8;
	ret += (pal32.p.g & 0xfc) << 3;
	ret += pal32.p.b >> 3;
	return(ret);
}

/* SUPPORT_TURBOZ: 4096 色モード切替。RGB565 直描きのため常に成功を返す */
BRESULT scrnmng_setcolormode(BOOL fullcolor) {

	(void)fullcolor;
	return(SUCCESS);
}

void scrnmng_setwidth(int posx, int width) {

	(void)posx;
	scrnstat.width = width;
}

void scrnmng_setheight(int posy, int height) {

	(void)posy;
	scrnstat.height = height;
}

const SCRNSURF *scrnmng_surflock(void) {

	SDL_Surface	*surface;

	surface = s_surface;
	if (surface == NULL) {
		return(NULL);
	}
	SDL_LockSurface(surface);
	scrnmng.surface = surface;
	scrnsurf.ptr = (BYTE *)surface->pixels;
	scrnsurf.xalign = surface->format->BytesPerPixel;
	scrnsurf.yalign = surface->pitch;
	scrnsurf.bpp = surface->format->BitsPerPixel;
	scrnsurf.width = min(scrnstat.width, 640);
	scrnsurf.height = min(scrnstat.height, 400);
	scrnsurf.extend = 0;
	return(&scrnsurf);
}

void scrnmng_surfunlock(const SCRNSURF *surf) {

	SDL_Surface	*surface;
	SDL_Surface	*winsurf;

	if (surf == NULL) {
		return;
	}
	surface = scrnmng.surface;
	if (surface == NULL) {
		return;
	}
	scrnmng.surface = NULL;
	SDL_UnlockSurface(surface);

	/* ウィンドウサーフェースはキャッシュしない (SDL 側で作り直されうる) */
	winsurf = SDL_GetWindowSurface(s_sdlWindow);
	if (winsurf != NULL) {
		SDL_BlitScaled(surface, NULL, winsurf, NULL);
		SDL_UpdateWindowSurface(s_sdlWindow);
	}
}


// ---- for menubase (スタブ: sysmenu がメニューを開かないため呼ばれない)

BOOL scrnmng_entermenu(SCRNMENU *smenu) {

	if (smenu) {
		smenu->width = scrnmng.width;
		smenu->height = scrnmng.height;
		smenu->bpp = scrnmng.bpp;
	}
	return(FAILURE);
}

void scrnmng_leavemenu(void) {
}

void scrnmng_menudraw(const RECT_T *rct) {

	(void)rct;
}
