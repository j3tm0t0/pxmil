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
#include <psppower.h>
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
#include "milstr.h"
#include "diskdrv.h"
#include "scrndraw.h"
#include "x1f.h"
#include "timing.h"
#include "sysmenu.h"
#include "selfexec.h"
#include "perf.h"
#include "joymng.h"

/* X1 のメモリ使用量は少ないので 16MB で十分 (PSP-1000 のユーザー空間は 24MB) */
PSP_HEAP_SIZE_KB(16384);

/* EBOOT 設置場所の既定値 (argv[0] が取れない場合のフォールバック) */
static const char default_exepath[] = "ms0:/PSP/GAME/PXMIL/EBOOT.PBP";

		XMILOSCFG	xmiloscfg = {0, 0};
static	UINT32		autotest_ms;	/* 0 = 通常起動 */
static	UINT32		boot_tick;
static	UINT		framecnt;
static	UINT		waitcnt;
static	UINT		framemax = 1;

#define	framereset(cnt)		framecnt = 0

/* pccore_exec を計測付きで呼ぶ */
static void exec_frame(BOOL draw) {

	UINT32	t0;

	t0 = perf_us();
	pccore_exec(draw);
	perf_add_exec(perf_us() - t0);
}

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

	/* 既定の 222MHz では間に合わないため最大クロックで動かす
	 * (px68k と同様。バスは CPU の半分)。 */
	scePowerSetClockFrequency(333, 333, 166);

	/* ファイルパスの基準を EBOOT のあるディレクトリにする */
	if ((argc > 0) && (argv[0] != NULL) && (strchr(argv[0], '/') != NULL)) {
		file_setcd(argv[0]);
	}
	else {
		file_setcd(default_exepath);
	}

	/* 自動テスト: EBOOT の隣に autotest ファイル (中身 = 秒数) があれば、
	 * その秒数だけ走って自動終了し、selfexec で pspbrew.dev へ戻る。
	 * tools/device-test.sh が使う。読んだら消すので手動起動には響かない。 */
	{
		FILEH fh = file_open_rb(file_getcd("autotest"));
		if (fh != FILEH_INVALID) {
			char buf[16];
			UINT r = file_read(fh, buf, sizeof(buf) - 1);
			file_close(fh);
			buf[(r < sizeof(buf)) ? r : 0] = '\0';
			autotest_ms = (UINT32)atoi(buf) * 1000;
			if (autotest_ms == 0) {
				autotest_ms = 45 * 1000;
			}
			file_delete(file_getcd("autotest"));
		}
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

	/* disk/ にある最初のディスクイメージを FDD0 にマウントして起動する
	 * (メニュー UI 実装までのつなぎ)。 */
	{
		FLINFO	fli;
		FLISTH	flh;

		flh = file_list1st(file_getcd("disk"), &fli);
		if (flh != FLISTH_INVALID) {
			do {
				const char *ext = file_getext(fli.path);
				if ((!file_cmpname(ext, "2d")) ||
					(!file_cmpname(ext, "d88")) ||
					(!file_cmpname(ext, "88d")) ||
					(!file_cmpname(ext, "2hd"))) {
					char path[MAX_PATH];
					milstr_ncpy(path, file_getcd("disk"), sizeof(path));
					file_setseparator(path, sizeof(path));
					milstr_ncat(path, fli.path, sizeof(path));
					diskdrv_setfdd(0, path, 0);
					break;
				}
			} while(file_listnext(flh, &fli) == SUCCESS);
			file_listclose(flh);
		}
	}

	scrndraw_redraw();
	pccore_reset();

	/* timing_setrate はこのソースツリーでは誰も呼ばず msstep=0 のまま
	 * (libretro は retro_run 駆動なので放置されている)。設定しないと
	 * timing_getcount() が永遠に 0 で、最初のフレーム描画後に
	 * processwait で止まる。4000<<16/clock が 1ms あたりの進み幅なので
	 * clock=66733 で約 59.94 カウント/秒 (≒X1 のフレームレート)。 */
	timing_setrate(66733);
	timing_reset();

	boot_tick = GETTICK();

	while(taskmng_isavail()) {
		taskmng_rol();
		perf_tick();
		if (autotest_ms != 0) {
			UINT32 el = GETTICK() - boot_tick;
			if (el >= autotest_ms) {
				taskmng_exit();
			}
			/* 本編の負荷を測るための自動入力: 12-14s でトリガー連打して
			 * ゲーム開始、16s 以降は移動 + 連射で遊んでいるふりをする。
			 * (負論理: ビットを落とす = 押下。0x40=ボタン1 0x04=左 0x08=右) */
			if ((el >= 12000) && (el < 14000)) {
				joy_autoinput = ((el / 250) & 1) ? (BYTE)~0x40 : 0xff;
			}
			else if (el >= 16000) {
				switch ((el / 400) % 4) {
				case 0:  joy_autoinput = (BYTE)~(0x40 | 0x04); break;
				case 1:  joy_autoinput = (BYTE)~0x08; break;
				case 2:  joy_autoinput = (BYTE)~(0x40 | 0x08); break;
				default: joy_autoinput = (BYTE)~0x04; break;
				}
			}
			else {
				joy_autoinput = 0xff;
			}
		}
		scrnmng_dbgtick();
		if (xmiloscfg.NOWAIT) {
			exec_frame(framecnt == 0);
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
				exec_frame(framecnt == 0);
				framecnt++;
			}
			else {
				processwait(xmiloscfg.DRAW_SKIP);
			}
		}
		else {									/* auto skip */
			if (!waitcnt) {
				UINT cnt;
				exec_frame(framecnt == 0);
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

	perf_dump();	/* pxmil.log に毎秒の計測値を書き出す */

	/* 実機では pspbrew.dev に戻る (テストサイクル短縮)。存在しない環境
	 * (PPSSPP 等) ではスキップしてそのまま終了する。 */
	{
		static const char pspbrew[] = "ms0:/PSP/GAME/pspbrew.dev/EBOOT.PBP";
		FILEH fh = file_open_rb(pspbrew);
		if (fh != FILEH_INVALID) {
			file_close(fh);
			exec_eboot(pspbrew);	/* 失敗時のみ戻ってくる */
		}
	}
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
