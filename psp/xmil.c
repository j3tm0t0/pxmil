/**
 * @file	xmil.c
 * @brief	X millennium PSP frontend - main loop
 *
 * SDL2main (libSDL2main.a) が module_info と exit callback を提供するため、
 * ここでは PSP_MODULE_INFO / PSP_MAIN_THREAD_ATTR を定義しないこと
 * (定義すると duplicate symbol でリンクに失敗する)。
 */

#include "compiler.h"
#include <SDL.h>
#include <pspmoduleinfo.h>
#include "strres.h"
#include "xmil.h"
#include "dosio.h"
#include "fontmng.h"
#include "scrnmng.h"
#include "soundmng.h"
#include "sysmng.h"
#include "taskmng.h"
#include "ini.h"
#include "pccore.h"
#include "iocore.h"
#include "scrndraw.h"
#include "x1f.h"
#include "timing.h"
#include "sysmenu.h"

/* X1 のメモリ使用量は少ないので 16MB で十分 (PSP-1000 のユーザー空間は 24MB) */
PSP_HEAP_SIZE_KB(16384);

/* EBOOT 設置場所の既定値 (argv[0] が取れない場合のフォールバック) */
static const char default_exepath[] = "ms0:/PSP/GAME/PXMIL/EBOOT.PBP";

		XMILOSCFG	xmiloscfg = {0, 0};
static	UINT		framecnt;
static	UINT		waitcnt;
static	UINT		framemax = 1;

#define	framereset(cnt)		framecnt = 0

static void processwait(UINT cnt) {

	if (timing_getcount() >= cnt) {
		timing_setcount(0);
		framereset(cnt);
	}
	else {
		taskmng_sleep(1);
	}
}

/* compiler.h (SDL.h) により main は SDL_main に置換され、
   libSDL2main.a 側の main() から呼ばれる */
int main(int argc, char *argv[]) {

	/* ファイルパスの基準を EBOOT のあるディレクトリにする */
	if ((argc > 0) && (argv[0] != NULL) && (strchr(argv[0], '/') != NULL)) {
		file_setcd(argv[0]);
	}
	else {
		file_setcd(default_exepath);
	}

	initload();

	TRACEINIT();

	if (fontmng_init() != SUCCESS) {
		goto xmilmain_err2;
	}

	if (sysmenu_create() != SUCCESS) {
		goto xmilmain_err3;
	}

	scrnmng_initialize();
	if (scrnmng_create(FULLSCREEN_WIDTH, FULLSCREEN_HEIGHT) != SUCCESS) {
		goto xmilmain_err4;
	}

	soundmng_initialize();
	sysmng_initialize();
	taskmng_initialize();
	pccore_initialize();

	scrndraw_redraw();
	pccore_reset();

	while(taskmng_isavail()) {
		taskmng_rol();
		if (xmiloscfg.NOWAIT) {
			pccore_exec(framecnt == 0);
			if (xmiloscfg.DRAW_SKIP) {			/* nowait frame skip */
				framecnt++;
				if (framecnt >= xmiloscfg.DRAW_SKIP) {
					processwait(0);
				}
			}
			else {								/* nowait auto skip */
				framecnt = 1;
				if (timing_getcount()) {
					processwait(0);
				}
			}
		}
		else if (xmiloscfg.DRAW_SKIP) {			/* frame skip */
			if (framecnt < xmiloscfg.DRAW_SKIP) {
				pccore_exec(framecnt == 0);
				framecnt++;
			}
			else {
				processwait(xmiloscfg.DRAW_SKIP);
			}
		}
		else {									/* auto skip */
			if (!waitcnt) {
				UINT cnt;
				pccore_exec(framecnt == 0);
				framecnt++;
				cnt = timing_getcount();
				if (framecnt > cnt) {
					waitcnt = framecnt;
					if (framemax > 1) {
						framemax--;
					}
				}
				else if (framecnt >= framemax) {
					if (framemax < 12) {
						framemax++;
					}
					if (cnt >= 12) {
						timing_reset();
					}
					else {
						timing_setcount(cnt - framecnt);
					}
					framereset(0);
				}
			}
			else {
				processwait(waitcnt);
				waitcnt = framecnt;
			}
		}
	}

	pccore_deinitialize();
	x1f_close();
	soundmng_deinitialize();

	sysmng_deinitialize();

	scrnmng_destroy();
	sysmenu_destroy();
	TRACETERM();
	SDL_Quit();
	return(SUCCESS);

xmilmain_err4:
	scrnmng_destroy();

xmilmain_err3:
	sysmenu_destroy();

xmilmain_err2:
	TRACETERM();
	SDL_Quit();
	return(FAILURE);
}
