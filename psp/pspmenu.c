/**
 * @file	pspmenu.c
 * @brief	PSP 用の専用メニュー画面 (px68k 風のリストメニュー)
 *
 * SELECT で開閉。上下で項目移動、左右で値の変更、○ で決定、× で戻る/
 * 閉じる。マウスカーソル方式の menubase は使わない。描画は present 時の
 * 合成バッファに全面で行い、文字は softkbd の 3x5 フォントの 3 倍スケール。
 * メニュー表示中もゲームは裏で動き続ける。
 */

#include	"compiler.h"
#include	"dosio.h"
#include	"pccore.h"
#include	"diskdrv.h"
#include	"milstr.h"
#include	"taskmng.h"
#include	"scrnmng.h"
#include	"softkbd.h"
#include	"pspmenu.h"

/* CPU クロック倍率 (baseclock 2MHz x multiple)。2=4MHz(実機) 3=6MHz 4=8MHz。
 * ゼビウス等は VSYNC 待ちでペースを取るため、クロックを上げると
 * ゲーム速度そのままで描き替えが毎フレーム間に合うようになる。 */
UINT8	pspcfg_clockmul = 2;

/* Z80 コアが命令ごとに消費するサイクルへの倍率 (256 = 等倍)。
 * フレーム構造・CTC・実時間は 4MHz ドメインのままなので、音楽テンポを
 * 変えずに CPU だけ速くなる。z80c/z80c.mcr の Z80_COUNT が参照。 */
UINT32	z80_cycmul = 256;

void pspmenu_applyclock(void) {

	if ((pspcfg_clockmul < 2) || (pspcfg_clockmul > 4)) {
		pspcfg_clockmul = 2;
	}
	z80_cycmul = 512 / pspcfg_clockmul;	/* 2→256(4MHz) 3→170(6MHz) 4→128(8MHz) */
}

/* ---- 状態 ---- */

enum {
	PAGE_MAIN = 0,
	PAGE_FILE				/* ディスク選択 (dir = disk/) */
};

enum {
	MID_FDD0 = 0,
	MID_FDD1,
	MID_EJECT0,
	MID_EJECT1,
	MID_RESET,
	MID_ASPECT,
	MID_OVERLAY,
	MID_CLOCK,
	MID_CLOSE,
	MID_EXIT,
	MID_MAX
};

static int		s_open;
static int		s_page;
static int		s_sel;				/* メイン画面の選択行 */
static int		s_fsel;				/* ファイル画面の選択行 */
static int		s_fdrive;			/* ファイル選択の対象ドライブ */
static int		s_ftop;				/* ファイル画面の表示先頭行 */

#define	MAXFILES	96
#define	NAMELEN		32
static char		s_files[MAXFILES][NAMELEN];
static int		s_nfiles;

static char		s_mounted[2][NAMELEN];	/* 表示用のマウント中ファイル名 */

/* ---- 操作 ---- */

int pspmenu_isopen(void) {

	return(s_open);
}

void pspmenu_toggle(void) {

	s_open ^= 1;
	s_page = PAGE_MAIN;
}

static void scanfiles(void) {

	FLINFO	fli;
	FLISTH	flh;

	s_nfiles = 0;
	flh = file_list1st(file_getcd("disk"), &fli);
	if (flh != FLISTH_INVALID) {
		do {
			const char *ext = file_getext(fli.path);
			if ((!file_cmpname(ext, "2d")) ||
				(!file_cmpname(ext, "d88")) ||
				(!file_cmpname(ext, "88d")) ||
				(!file_cmpname(ext, "2hd"))) {
				if (s_nfiles < MAXFILES) {
					milstr_ncpy(s_files[s_nfiles], fli.path, NAMELEN);
					s_nfiles++;
				}
			}
		} while(file_listnext(flh, &fli) == SUCCESS);
		file_listclose(flh);
	}
}

static void mount(int drive, const char *name) {

	char	path[MAX_PATH];

	milstr_ncpy(path, file_getcd("disk"), sizeof(path));
	file_setseparator(path, sizeof(path));
	milstr_ncat(path, name, sizeof(path));
	diskdrv_setfdd((REG8)drive, path, 0);
	milstr_ncpy(s_mounted[drive], name, NAMELEN);
}

static void eject(int drive) {

	diskdrv_setfdd((REG8)drive, NULL, 0);
	s_mounted[drive][0] = '\0';
}

static void decide_main(void) {

	switch(s_sel) {
		case MID_FDD0:
		case MID_FDD1:
			s_fdrive = (s_sel == MID_FDD1) ? 1 : 0;
			scanfiles();
			s_fsel = 0;
			s_ftop = 0;
			s_page = PAGE_FILE;
			break;
		case MID_EJECT0:
			eject(0);
			break;
		case MID_EJECT1:
			eject(1);
			break;
		case MID_RESET:
			pccore_reset();
			pspmenu_applyclock();
			s_open = 0;
			break;
		case MID_ASPECT:
			scrnmng_nextaspect();
			break;
		case MID_OVERLAY:
			pspcfg_overlay ^= 1;
			break;
		case MID_CLOCK:
			pspcfg_clockmul = (UINT8)((pspcfg_clockmul - 1) % 3 + 2);
			pspmenu_applyclock();
			break;
		case MID_CLOSE:
			s_open = 0;
			break;
		case MID_EXIT:
			taskmng_exit();
			break;
	}
}

/* 上下左右・決定・戻る。戻り値 0 でメニューが閉じた */
void pspmenu_input(int dx, int dy, int decide, int back) {

	if (s_page == PAGE_MAIN) {
		if (dy) {
			s_sel = (s_sel + dy + MID_MAX) % MID_MAX;
		}
		if (dx) {
			if (s_sel == MID_ASPECT) {
				scrnmng_nextaspect();
			}
			if (s_sel == MID_OVERLAY) {
				pspcfg_overlay ^= 1;
			}
			if (s_sel == MID_CLOCK) {
				pspcfg_clockmul = (UINT8)((pspcfg_clockmul - 1) % 3 + 2);
				pspmenu_applyclock();
			}
		}
		if (decide) {
			decide_main();
		}
		if (back) {
			s_open = 0;
		}
	}
	else {		/* PAGE_FILE */
		if ((dy) && (s_nfiles > 0)) {
			s_fsel = (s_fsel + dy + s_nfiles) % s_nfiles;
		}
		if (decide && (s_nfiles > 0)) {
			mount(s_fdrive, s_files[s_fsel]);
			s_page = PAGE_MAIN;
		}
		if (back) {
			s_page = PAGE_MAIN;
		}
	}
}

/* ---- 描画 ---- */

#define	COL_PANEL	0x10a2
#define	COL_TITLE	0x07ff
#define	COL_ITEM	0xffff
#define	COL_DIM		0x8410
#define	COL_SELBG	0xffff
#define	COL_SELFG	0x0000

#define	SCALE		3
#define	LINEH		(6 * SCALE)		/* 行高 */
#define	PX			40				/* パネル左上 */
#define	PY			40
#define	PW			560
#define	PH			320

static void drawitem(UINT16 *dst, int line, const char *text, int sel,
																UINT16 col) {

	int	y = PY + 14 + line * LINEH;

	if (sel) {
		skb_fillrect(dst, PX + 8, y - 2, PW - 16, LINEH, COL_SELBG);
		skb_drawtext(dst, PX + 16, y, text, COL_SELFG, SCALE);
	}
	else {
		skb_drawtext(dst, PX + 16, y, text, col, SCALE);
	}
}

void pspmenu_draw(UINT16 *dst) {

	char	buf[64];
	int		i;

	if (!s_open) {
		return;
	}
	skb_fillrect(dst, PX, PY, PW, PH, COL_PANEL);

	if (s_page == PAGE_MAIN) {
#ifndef PXMIL_VER
#define PXMIL_VER "dev"
#endif
		skb_drawtext(dst, PX + 16, PY + 4,
					"X MILLENNIUM MENU  VER " PXMIL_VER, COL_TITLE, 2);
		sprintf(buf, "FDD0: %s", (s_mounted[0][0]) ? s_mounted[0] : "<EMPTY>");
		drawitem(dst, 1, buf, (s_sel == MID_FDD0), COL_ITEM);
		sprintf(buf, "FDD1: %s", (s_mounted[1][0]) ? s_mounted[1] : "<EMPTY>");
		drawitem(dst, 2, buf, (s_sel == MID_FDD1), COL_ITEM);
		drawitem(dst, 3, "EJECT FDD0", (s_sel == MID_EJECT0), COL_ITEM);
		drawitem(dst, 4, "EJECT FDD1", (s_sel == MID_EJECT1), COL_ITEM);
		drawitem(dst, 5, "RESET", (s_sel == MID_RESET), COL_ITEM);
		{
			static const char *asp[] = {"DOT 8:5", "MONITOR 4:3", "STRETCH"};
			sprintf(buf, "ASPECT: < %s >", asp[pspcfg_aspect % 3]);
			drawitem(dst, 6, buf, (s_sel == MID_ASPECT), COL_ITEM);
		}
		sprintf(buf, "FPS DISPLAY: < %s >", (pspcfg_overlay) ? "ON" : "OFF");
		drawitem(dst, 7, buf, (s_sel == MID_OVERLAY), COL_ITEM);
		sprintf(buf, "CPU CLOCK: < %dMHZ >", pspcfg_clockmul * 2);
		drawitem(dst, 8, buf, (s_sel == MID_CLOCK), COL_ITEM);
		drawitem(dst, 9, "CLOSE MENU", (s_sel == MID_CLOSE), COL_ITEM);
		drawitem(dst, 10, "EXIT EMULATOR", (s_sel == MID_EXIT), COL_ITEM);
		skb_drawtext(dst, PX + 16, PY + PH - 14,
			"UP/DOWN:MOVE  O:OK  X:CLOSE", COL_DIM, 2);
	}
	else {
		int	nvis = 13;

		sprintf(buf, "SELECT DISK FOR FDD%d", s_fdrive);
		skb_drawtext(dst, PX + 16, PY + 4, buf, COL_TITLE, 2);
		if (s_nfiles == 0) {
			drawitem(dst, 1, "<NO DISK IMAGES IN disk/>", 0, COL_DIM);
		}
		if (s_fsel < s_ftop) {
			s_ftop = s_fsel;
		}
		if (s_fsel >= s_ftop + nvis) {
			s_ftop = s_fsel - nvis + 1;
		}
		for (i = 0; i < nvis; i++) {
			int	n = s_ftop + i;
			if (n >= s_nfiles) {
				break;
			}
			drawitem(dst, 1 + i, s_files[n], (n == s_fsel), COL_ITEM);
		}
		skb_drawtext(dst, PX + 16, PY + PH - 14,
			"UP/DOWN:MOVE  O:MOUNT  X:BACK", COL_DIM, 2);
	}
}

/* 起動時自動マウントの表示名を xmil.c から知らせる */
void pspmenu_setmounted(int drive, const char *name) {

	milstr_ncpy(s_mounted[drive], name, NAMELEN);
}
