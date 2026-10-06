/**
 * @file	taskmng.c
 * @brief	PSP 用タスクマネージャ
 *
 * SDL イベント処理 (HOME ボタン → exit callback → SDL_QUIT) と、
 * sceCtrl によるボタン入力を行う。
 *   SELECT : メニュー開閉 (終了はメニューの Exit から)
 *   START  : リセット
 *   L      : ソフトキーボード開閉 / L+R 同時 : ○× 入れ替え
 *   R      : アスペクトモード切替 (ドット等倍 / 4:3 / 引き伸ばし)
 *   △/□   : ○/× の連射 (joymng.c)
 *
 * メニューは px68k 風のリスト画面 (psp/pspmenu.c)。上下で項目移動、
 * ○ で決定、× で戻る。メニュー/キーボード中はゲームへのパッド入力は
 * 止まる。
 */

#include	"compiler.h"
#include	<SDL.h>
#include	<pspctrl.h>
#include	"taskmng.h"
#include	"pccore.h"
#include	"scrnmng.h"
#include	"joymng.h"
#include	"pspmenu.h"
#include	"softkbd.h"

	BOOL	task_avail;

static	UINT32	lastbuttons;


void taskmng_initialize(void) {

	task_avail = TRUE;
	lastbuttons = 0;
	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);	/* joymng がスティックも見る */
}

void taskmng_exit(void) {

	task_avail = FALSE;
}

void taskmng_rol(void) {

	SDL_Event	e;
	SceCtrlData	pad;
	UINT32		pressed;
	UINT32		released;

	while(SDL_PollEvent(&e)) {
		switch(e.type) {
			case SDL_QUIT:
				task_avail = FALSE;
				break;
		}
	}

	if (sceCtrlPeekBufferPositive(&pad, 1) <= 0) {
		return;
	}
	pressed = pad.Buttons & ~lastbuttons;
	released = lastbuttons & ~pad.Buttons;
	lastbuttons = pad.Buttons;

	if (pressed & PSP_CTRL_SELECT) {
		pspmenu_toggle();
		scrnmng_menupresent();
		return;
	}

	if (pspmenu_isopen()) {
		/* リストメニュー操作 (エッジ + リピート) */
		static UINT32 nextrep;
		UINT32	now = GETTICK();
		int		dx = 0, dy = 0;
		UINT32	dirs = pad.Buttons &
					(PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT);

		if ((pressed & dirs) || (dirs && (now >= nextrep))) {
			if (dirs & PSP_CTRL_UP)    dy = -1;
			if (dirs & PSP_CTRL_DOWN)  dy = 1;
			if (dirs & PSP_CTRL_LEFT)  dx = -1;
			if (dirs & PSP_CTRL_RIGHT) dx = 1;
			nextrep = now + ((pressed & dirs) ? 300 : 120);
		}
		if (dx || dy || (pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS))) {
			pspmenu_input(dx, dy,
				(pressed & PSP_CTRL_CIRCLE) ? 1 : 0,
				(pressed & PSP_CTRL_CROSS) ? 1 : 0);
			scrnmng_menupresent();
		}
		return;
	}

	if (softkbd_isvisible()) {
		/* カーソル移動 (エッジ + 150ms リピート) */
		static UINT32 nextrep;
		UINT32	now = GETTICK();
		int		dx = 0, dy = 0;
		UINT32	dirs = pad.Buttons &
					(PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT);

		if ((pressed & dirs) || (dirs && (now >= nextrep))) {
			if (dirs & PSP_CTRL_LEFT)  dx = -1;
			if (dirs & PSP_CTRL_RIGHT) dx = 1;
			if (dirs & PSP_CTRL_UP)    dy = -1;
			if (dirs & PSP_CTRL_DOWN)  dy = 1;
			nextrep = now + ((pressed & dirs) ? 300 : 120);
			softkbd_move(dx, dy);
			scrnmng_menupresent();
		}
		if (pressed & PSP_CTRL_CIRCLE) {
			softkbd_press();
			scrnmng_menupresent();
		}
		if (released & PSP_CTRL_CIRCLE) {
			softkbd_release();
		}
		if (pressed & PSP_CTRL_CROSS) {
			softkbd_toggle();		/* ×: 閉じる */
			scrnmng_menupresent();
		}
		return;
	}

	if (pressed & PSP_CTRL_START) {
		pccore_reset();
	}
	if ((pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) &&
		((pad.Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) ==
						(PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER))) {
		xmilcfg.BTN_MODE ^= 1;		/* L+R 同時: ○× 入れ替え (コア実装) */
	}
	else if (pressed & PSP_CTRL_RTRIGGER) {
		scrnmng_nextaspect();		/* R: アスペクト切替 */
	}
	else if (pressed & PSP_CTRL_LTRIGGER) {
		softkbd_toggle();			/* L: ソフトキーボード開閉 */
		scrnmng_menupresent();
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
