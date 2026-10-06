/**
 * @file	joymng.c
 * @brief	PSP 用ジョイスティック入力
 *
 * io/sndboard.c が PSG reg 0x0e の読み出しで joymng_getstat() を参照する。
 * ビット配置は nds/libretro 版と同じ (負論理、1 = 離している):
 *   bit0 = 上, bit1 = 下, bit2 = 左, bit3 = 右
 *   bit6 = ボタン1 (○), bit5 = ボタン2 (×), bit7 = ボタン3 (△), bit4 = ボタン4 (□)
 * アナログスティックもデジタル方向に変換する。
 */

#include	"compiler.h"
#include	<pspctrl.h>
#include	"joymng.h"

#define	JOY_UP_BIT		0x01
#define	JOY_DOWN_BIT	0x02
#define	JOY_LEFT_BIT	0x04
#define	JOY_RIGHT_BIT	0x08
#define	JOY_BTN1_BIT	0x40
#define	JOY_BTN2_BIT	0x20
#define	JOY_BTN3_BIT	0x80
#define	JOY_BTN4_BIT	0x10

#define	ANALOG_THRESHOLD	64

/* autotest の自動入力 (負論理マスク、0xff = 入力なし)。xmil.c が設定 */
BYTE	joy_autoinput = 0xff;

/* L トリガーで切替 (taskmng.c が設定) */
BYTE	joy_autofire = 0;		/* 1 = ○ を押しっぱなしで 15 連射 */
BYTE	joy_swapbtn = 0;		/* 1 = ○× (ボタン1/2) を入れ替え */

BYTE joymng_getstat(void) {

	SceCtrlData	pad;
	BYTE		ret;
	UINT32		fire1, fire2;

	ret = joy_autoinput;
	if (sceCtrlPeekBufferPositive(&pad, 1) <= 0) {
		return(ret);
	}
	fire1 = joy_swapbtn ? JOY_BTN2_BIT : JOY_BTN1_BIT;
	fire2 = joy_swapbtn ? JOY_BTN1_BIT : JOY_BTN2_BIT;
	if (pad.Buttons & PSP_CTRL_UP) {
		ret &= (BYTE)~JOY_UP_BIT;
	}
	if (pad.Buttons & PSP_CTRL_DOWN) {
		ret &= (BYTE)~JOY_DOWN_BIT;
	}
	if (pad.Buttons & PSP_CTRL_LEFT) {
		ret &= (BYTE)~JOY_LEFT_BIT;
	}
	if (pad.Buttons & PSP_CTRL_RIGHT) {
		ret &= (BYTE)~JOY_RIGHT_BIT;
	}
	if (pad.Ly < (128 - ANALOG_THRESHOLD)) {
		ret &= (BYTE)~JOY_UP_BIT;
	}
	if (pad.Ly > (128 + ANALOG_THRESHOLD)) {
		ret &= (BYTE)~JOY_DOWN_BIT;
	}
	if (pad.Lx < (128 - ANALOG_THRESHOLD)) {
		ret &= (BYTE)~JOY_LEFT_BIT;
	}
	if (pad.Lx > (128 + ANALOG_THRESHOLD)) {
		ret &= (BYTE)~JOY_RIGHT_BIT;
	}
	if (pad.Buttons & PSP_CTRL_CIRCLE) {
		/* 連射: 押している間 30ms ON / 30ms OFF (約 16 連/秒)。
		 * X1 側は毎フレーム読むので 60Hz 未満の矩形波にする */
		if ((!joy_autofire) || ((GETTICK() / 30) & 1)) {
			ret &= (BYTE)~fire1;
		}
	}
	if (pad.Buttons & PSP_CTRL_CROSS) {
		ret &= (BYTE)~fire2;
	}
	if (pad.Buttons & PSP_CTRL_TRIANGLE) {
		ret &= (BYTE)~JOY_BTN3_BIT;
	}
	if (pad.Buttons & PSP_CTRL_SQUARE) {
		ret &= (BYTE)~JOY_BTN4_BIT;
	}
	return(ret);
}
