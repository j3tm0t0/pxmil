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
#include	"perf.h"

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

/* アスペクトモード (R トリガーで巡回切替) */
enum {
	ASPECT_DOT = 0,		/* ドット等倍比 8:5 */
	ASPECT_MONITOR,		/* 実機モニタ比 4:3 */
	ASPECT_STRETCH		/* 全画面引き伸ばし */
};
static int aspect_mode = ASPECT_DOT;
static int border_clear = 2;	/* 黒帯を塗り直す残り回数 (切替時に再セット) */

/* ---- デバッグオーバーレイ (左上に loop/draw/fps を表示) ----
 * メインループから毎周 scrnmng_dbgtick() を呼ぶ。ループが回っていれば
 * 数字が増え、コアが描画していれば draw も増える。どちらも止まって
 * いれば表示自体が更新されない — という切り分けができる。 */

static UINT32	dbg_loopcnt;
static UINT32	dbg_drawcnt;

/* 3x5 の数字フォント (各行 3bit、上から 5 行) */
static const UINT8 dbgfont[10][5] = {
	{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
	{7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
};

static void dbg_drawnum(SDL_Surface *s, int x, int y, UINT32 val, int digits) {

	int		d, row, col;
	UINT16	*p;

	for (d = digits - 1; d >= 0; d--) {
		UINT32 v = val % 10;
		val /= 10;
		for (row = 0; row < 5; row++) {
			p = (UINT16 *)((UINT8 *)s->pixels + (y + row) * s->pitch) + x + d * 4;
			for (col = 0; col < 3; col++) {
				p[col] = (dbgfont[v][row] & (4 >> col)) ? 0xffff : 0x0000;
			}
			p[3] = 0;
		}
	}
}

static UINT32	dbg_lastpresent;	/* 最後に present した時刻 (surfunlock が更新) */

/* オーバーレイを winsurf に描き込む (present はしない。present は
 * surfunlock の 1 箇所に統一 — PSP の SDL2 はダブルバッファで、複数箇所
 * から update するとスワップが交互に走り、古いフレームのバッファが
 * 表に出てチカチカするため)。 */
static void dbg_render(SDL_Surface *winsurf) {

	SDL_LockSurface(winsurf);
	dbg_drawnum(winsurf, 2, 2, dbg_loopcnt % 100000, 5);		/* ループ生存 */
	dbg_drawnum(winsurf, 2, 9, perf_now.execps, 3);			/* exec/s */
	dbg_drawnum(winsurf, 2, 16, perf_now.drawps, 3);			/* 表示 fps */
	dbg_drawnum(winsurf, 2, 23, perf_now.execus / 100, 4);	/* exec 平均 0.1ms */
	dbg_drawnum(winsurf, 2, 30, perf_now.presus / 100, 4);	/* present 平均 0.1ms */
	SDL_UnlockSurface(winsurf);
}

static void present_frame(void);

void scrnmng_dbgtick(void) {

	UINT32	now;

	dbg_loopcnt++;
	now = SDL_GetTicks();

	/* コアが 500ms 以上描画していないときだけ、こちらから present して
	 * 生存表示を続ける (停止の切り分け用)。通常時は surfunlock に任せる。
	 * 内容は surfunlock と同じ「最終ゲームフレーム + オーバーレイ」。
	 * 黒塗り + オーバーレイだけを出すと、フレームスキップで描画間隔が
	 * 500ms を超えたときにゲーム画面と黒画面が交互に出てチカチカする。 */
	if ((now - dbg_lastpresent) < 500) {
		return;
	}
	present_frame();
}

/* 最終ゲームフレーム (s_surface) をアスペクトモードに従って winsurf に
 * 縮小転送し、オーバーレイを重ねて present する。present はこの関数の
 * 1 箇所のみ (surfunlock と dbgtick のフォールバックが共用)。 */
static void present_frame(void) {

	SDL_Surface	*winsurf;
	SDL_Rect	src, dst;
	int			w, h;
	int			aw, ah;
	UINT32		t0;

	t0 = perf_us();

	/* ウィンドウサーフェースはキャッシュしない (SDL 側で作り直されうる) */
	winsurf = SDL_GetWindowSurface(s_sdlWindow);
	if ((winsurf == NULL) || (s_surface == NULL)) {
		return;
	}

	/* アスペクトモードに従って 480x272 に収める。表示モードで
	 * source サイズが変わっても scrnstat に追従する。 */
	src.x = 0;
	src.y = 0;
	src.w = min(scrnstat.width, 640);
	src.h = min(scrnstat.height, 400);
	switch(aspect_mode) {
	case ASPECT_DOT:		/* ドット等倍比 (640x400 → 435x272) */
		aw = src.w;
		ah = src.h;
		break;
	case ASPECT_MONITOR:	/* 実機モニタ 4:3 (→ 362x272) */
		aw = 4;
		ah = 3;
		break;
	default:				/* 480x272 引き伸ばし */
		aw = PSP_SCREEN_WIDTH;
		ah = PSP_SCREEN_HEIGHT;
		break;
	}
	w = PSP_SCREEN_WIDTH;
	h = w * ah / aw;
	if (h > PSP_SCREEN_HEIGHT) {
		h = PSP_SCREEN_HEIGHT;
		w = h * aw / ah;
	}
	dst.x = (PSP_SCREEN_WIDTH - w) / 2;
	dst.y = (PSP_SCREEN_HEIGHT - h) / 2;
	dst.w = w;
	dst.h = h;
	/* レターボックスの黒帯は起動直後とアスペクト切替直後だけ塗る。
	 * PSP の SDL2 ではウィンドウサーフェースが表示中の VRAM に近く、
	 * 毎フレーム FillRect すると「黒塗り→ブリット」の中間状態が画面に
	 * 見えて盛大にチカチカする (実機で確認。帯のないストレッチモード
	 * だけチラつかないのが決め手だった)。ゲーム矩形はブリットが毎回
	 * 全面上書きするので塗り直し不要。保険でダブルバッファ両面分の
	 * 2 回塗る。 */
	if (border_clear > 0) {
		border_clear--;
		SDL_FillRect(winsurf, NULL, 0);
	}
	SDL_BlitScaled(s_surface, &src, winsurf, &dst);
	dbg_render(winsurf);
	SDL_UpdateWindowSurface(s_sdlWindow);
	dbg_lastpresent = SDL_GetTicks();
	perf_add_present(perf_us() - t0);
}

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

	if (surf == NULL) {
		return;
	}
	surface = scrnmng.surface;
	if (surface == NULL) {
		return;
	}
	scrnmng.surface = NULL;
	SDL_UnlockSurface(surface);
	present_frame();
	dbg_drawcnt++;
}

void scrnmng_nextaspect(void) {

	aspect_mode = (aspect_mode + 1) % 3;
	border_clear = 2;
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
