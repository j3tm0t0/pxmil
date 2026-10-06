/**
 * @file	taskmng.c
 * @brief	PSP 用タスクマネージャ
 *
 * SDL イベント処理 (HOME ボタン → exit callback → SDL_QUIT) と、
 * sceCtrl によるボタン入力を行う。
 *   SELECT : メニュー開閉 (終了はメニューの Exit から)
 *   START  : リセット
 *   L      : 連射トグル / L+R 同時 : ○× 入れ替え
 *   R      : アスペクトモード切替 (ドット等倍 / 4:3 / 引き伸ばし)
 *
 * メニュー表示中は D-pad/アナログでカーソルを動かし、○ で決定
 * (menubase へマウスとして渡す)。ゲームへのパッド入力は止まる。
 */

#include	"compiler.h"
#include	<SDL.h>
#include	<pspctrl.h>
#include	"taskmng.h"
#include	"pccore.h"
#include	"scrnmng.h"
#include	"joymng.h"
#include	"sysmenu.h"
#include	"menubase.h"

	BOOL	task_avail;

static	UINT32	lastbuttons;

/* メニューカーソル (640x400 座標系) */
static	int		cur_x = 320;
static	int		cur_y = 200;

void taskmng_initialize(void) {

	task_avail = TRUE;
	lastbuttons = 0;
	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);	/* joymng がスティックも見る */
}

void taskmng_exit(void) {

	task_avail = FALSE;
}

/* メニュー表示中の入力: カーソル移動 + ○ クリックを menubase に渡す */
static void menu_input(const SceCtrlData *pad, UINT32 pressed, UINT32 released) {

	int		dx = 0, dy = 0;
	int		moved;

	if (pad->Buttons & PSP_CTRL_LEFT) {
		dx -= 3;
	}
	if (pad->Buttons & PSP_CTRL_RIGHT) {
		dx += 3;
	}
	if (pad->Buttons & PSP_CTRL_UP) {
		dy -= 3;
	}
	if (pad->Buttons & PSP_CTRL_DOWN) {
		dy += 3;
	}
	if (pad->Lx < 64) {
		dx -= 4;
	}
	if (pad->Lx > 192) {
		dx += 4;
	}
	if (pad->Ly < 64) {
		dy -= 4;
	}
	if (pad->Ly > 192) {
		dy += 4;
	}
	moved = (dx | dy);
	cur_x += dx;
	cur_y += dy;
	if (cur_x < 0) cur_x = 0;
	if (cur_x > 639) cur_x = 639;
	if (cur_y < 0) cur_y = 0;
	if (cur_y > 399) cur_y = 399;

	if (pressed & PSP_CTRL_CIRCLE) {
		menubase_moving(cur_x, cur_y, 1);		/* 左ボタン down */
	}
	else if (released & PSP_CTRL_CIRCLE) {
		menubase_moving(cur_x, cur_y, 2);		/* 左ボタン up */
	}
	else if (moved) {
		menubase_moving(cur_x, cur_y, 0);
	}
	if (moved || (pressed & PSP_CTRL_CIRCLE) || (released & PSP_CTRL_CIRCLE)) {
		scrnmng_menupresent();
	}
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
		if (menuvram == NULL) {
			sysmenu_menuopen(0, 0, 0);
			scrnmng_menupresent();
		}
		else {
			menubase_close();
			scrnmng_menupresent();
		}
		return;
	}

	if (menuvram != NULL) {
		menu_input(&pad, pressed, released);
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
		xmilcfg.BTN_RAPID ^= 1;		/* L: 連射トグル (コア実装) */
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
