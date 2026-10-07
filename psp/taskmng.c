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
#include	"keystat.h"

/* ---- KEYPAD モード: パッドをキーボードに割り当てる ----
 * D-pad/アナログ = テンキー (斜めは 1379)、□=Z ×=SPACE ○=X
 * △=CTRL+W (Brain Breaker のワープエンジン)。 */

#define	NKEY_W		0x11
#define	NKEY_Z		0x29
#define	NKEY_X_	0x2a
#define	NKEY_SPACE	0x34
#define	NKEY_CTRL	0x74
static const UINT8 kp_dir[16] = {	/* bit0=上 bit1=下 bit2=左 bit3=右 */
	0x00, 0x43, 0x4b, 0x00, 0x46, 0x42, 0x4a, 0x46,
	0x48, 0x44, 0x4c, 0x48, 0x00, 0x43, 0x4b, 0x00 };

static	UINT8	kp_curdir;			/* 押下中のテンキーコード (0=なし) */
static	UINT32	kp_buttons;			/* 押下中のボタン (PSP ビット) */

static void keypad_key(UINT32 btn, UINT32 now, UINT8 code) {

	UINT32 was = kp_buttons & btn;
	if ((now & btn) && !was) {
		keystat_keydown(code);
	}
	else if (!(now & btn) && was) {
		keystat_keyup(code);
	}
}

static void keypad_input(const SceCtrlData *pad) {

	UINT32	now = pad->Buttons;
	UINT	d = 0;
	UINT8	code;

	if ((pad->Buttons & PSP_CTRL_UP) || (pad->Ly < 64)) d |= 1;
	if ((pad->Buttons & PSP_CTRL_DOWN) || (pad->Ly > 192)) d |= 2;
	if ((pad->Buttons & PSP_CTRL_LEFT) || (pad->Lx < 64)) d |= 4;
	if ((pad->Buttons & PSP_CTRL_RIGHT) || (pad->Lx > 192)) d |= 8;
	code = kp_dir[d];
	if (code != kp_curdir) {
		if (kp_curdir) {
			keystat_keyup(kp_curdir);
		}
		if (code) {
			keystat_keydown(code);
		}
		kp_curdir = code;
	}

	/* △=Z (つい押して武器を捨てがちなので誤爆しにくい側に)、
	 * □=CTRL+W (タンク搭乗時以外は無害) */
	keypad_key(PSP_CTRL_TRIANGLE, now, NKEY_Z);
	keypad_key(PSP_CTRL_CROSS, now, NKEY_SPACE);
	keypad_key(PSP_CTRL_CIRCLE, now, NKEY_X_);
	keypad_key(PSP_CTRL_SQUARE, now, NKEY_CTRL);
	keypad_key(PSP_CTRL_SQUARE, now, NKEY_W);
	kp_buttons = now;
}

/* モード切替/メニュー遷移時に押しっぱなしを解放する */
void keypad_releaseall(void) {

	if (kp_curdir) {
		keystat_keyup(kp_curdir);
		kp_curdir = 0;
	}
	kp_buttons = 0;
	keystat_allrelease();
}

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

	if (pspcfg_keymode) {
		keypad_input(&pad);
	}

	if (pressed & PSP_CTRL_START) {
		pccore_reset();
		pspmenu_applyclock();
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
