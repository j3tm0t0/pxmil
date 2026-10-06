/**
 * @file	taskmng.c
 * @brief	PSP 用タスクマネージャ
 *
 * SDL イベント処理 (HOME ボタン → exit callback → SDL_QUIT) と、
 * sceCtrl による最小限のボタン入力を行う。
 *   START  : リセット
 *   SELECT : 終了 (XMB へ戻る)
 */

#include	"compiler.h"
#include	<SDL.h>
#include	<pspctrl.h>
#include	"taskmng.h"
#include	"pccore.h"

	BOOL	task_avail;

static	UINT32	lastbuttons;

void taskmng_initialize(void) {

	task_avail = TRUE;
	lastbuttons = 0;
	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
}

void taskmng_exit(void) {

	task_avail = FALSE;
}

void taskmng_rol(void) {

	SDL_Event	e;
	SceCtrlData	pad;
	UINT32		pressed;

	while(SDL_PollEvent(&e)) {
		switch(e.type) {
			case SDL_QUIT:
				task_avail = FALSE;
				break;
		}
	}

	if (sceCtrlPeekBufferPositive(&pad, 1) > 0) {
		pressed = pad.Buttons & ~lastbuttons;
		lastbuttons = pad.Buttons;
		if (pressed & PSP_CTRL_START) {
			pccore_reset();
		}
		if (pressed & PSP_CTRL_SELECT) {
			task_avail = FALSE;
		}
	}
}

BOOL taskmng_sleep(UINT32 tick) {

	UINT32	base;

	base = GETTICK();
	while((task_avail) && ((GETTICK() - base) < tick)) {
		taskmng_rol();
		SDL_Delay(1);
	}
	return(task_avail);
}
