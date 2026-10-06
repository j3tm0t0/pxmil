/**
 * @file	softkbd.c
 * @brief	PSP 用ソフトウェアキーボード
 *
 * △ で開閉 (taskmng.c)。D-pad/アナログでカーソル移動、○ で押下
 * (押している間キーも押しっぱなし)、× で閉じる。SHIFT/CTRL/KANA/GRPH
 * はロック式トグル。キーは keystat_keydown/keyup で X1 のキーマトリクス
 * に注入される。描画は present 時に合成バッファへ直接行う
 * (scrnmng.c の compose 経路)。ラベルは自前の 3x5 ピクセルフォント。
 */

#include	"compiler.h"
#include	"keystat.h"
#include	"softkbd.h"

/* NKEY コード (keystat.h の enum は #if 0 なので必要分をここで定義) */
#define	NKEY_ESC		0x00
#define	NKEY_BACKSPACE	0x0e
#define	NKEY_TAB		0x0f
#define	NKEY_RETURN		0x1c
#define	NKEY_SPACE		0x34
#define	NKEY_UP			0x3a
#define	NKEY_LEFT		0x3b
#define	NKEY_RIGHT		0x3c
#define	NKEY_DOWN		0x3d
#define	NKEY_HOMECLR	0x3e
#define	NKEY_INS		0x38
#define	NKEY_DEL		0x39
#define	NKEY_F1			0x62
#define	NKEY_SHIFT		0x70
#define	NKEY_CAPS		0x71
#define	NKEY_KANA		0x72
#define	NKEY_GRPH		0x73
#define	NKEY_CTRL		0x74

typedef struct {
	const char	*label;
	UINT8		code;
	UINT8		w;			/* セル幅 (1 = KEYW px) */
} SKEY;

#define	K(l, c)		{l, c, 1}
#define	KW(l, c, w)	{l, c, w}

static const SKEY row0[] = {
	K("ES",0x00), K("1",0x01), K("2",0x02), K("3",0x03), K("4",0x04),
	K("5",0x05), K("6",0x06), K("7",0x07), K("8",0x08), K("9",0x09),
	K("0",0x0a), K("-",0x0b), K("^",0x0c), K("\\",0x0d), KW("BS",NKEY_BACKSPACE,2) };
static const SKEY row1[] = {
	KW("TB",NKEY_TAB,2), K("Q",0x10), K("W",0x11), K("E",0x12), K("R",0x13),
	K("T",0x14), K("Y",0x15), K("U",0x16), K("I",0x17), K("O",0x18),
	K("P",0x19), K("@",0x1a), K("[",0x1b), KW("RET",NKEY_RETURN,2) };
static const SKEY row2[] = {
	KW("CT",NKEY_CTRL,2), K("A",0x1d), K("S",0x1e), K("D",0x1f), K("F",0x20),
	K("G",0x21), K("H",0x22), K("J",0x23), K("K",0x24), K("L",0x25),
	K(";",0x26), K(":",0x27), KW("]",0x28,3) };
static const SKEY row3[] = {
	KW("SH",NKEY_SHIFT,2), K("Z",0x29), K("X",0x2a), K("C",0x2b), K("V",0x2c),
	K("B",0x2d), K("N",0x2e), K("M",0x2f), K(",",0x30), K(".",0x31),
	K("/",0x32), K("_",0x33), KW("SH",NKEY_SHIFT,3) };
static const SKEY row4[] = {
	KW("KA",NKEY_KANA,2), KW("GR",NKEY_GRPH,2), KW("SPACE",NKEY_SPACE,6),
	KW("HM",NKEY_HOMECLR,2), K("IN",NKEY_INS), KW("DL",NKEY_DEL,3) };
static const SKEY row5[] = {
	KW("F1",0x62,2), KW("F2",0x63,2), KW("F3",0x64,2), KW("F4",0x65,2),
	KW("F5",0x66,2), KW("<",NKEY_LEFT,1), KW("v",NKEY_DOWN,1),
	KW("^",NKEY_UP,1), KW(">",NKEY_RIGHT,3) };

static const SKEY	*rows[] = {row0, row1, row2, row3, row4, row5};
static const int	rowlen[] = {
	NELEMENTS(row0), NELEMENTS(row1), NELEMENTS(row2),
	NELEMENTS(row3), NELEMENTS(row4), NELEMENTS(row5)};
#define	NROWS	6

#define	KEYW	20			/* セル幅 1 単位 (px) */
#define	KEYH	14
#define	KBD_W	(16 * KEYW + 2)
#define	KBD_X	((640 - KBD_W) / 2)	/* 中央寄せ */
#define	KBD_H	(NROWS * KEYH + 2)
#define	KBD_Y	(400 - KBD_H)

static int		s_visible;
static int		s_row, s_col;
static UINT8	s_pressed = 0xff;		/* ○ で押下中のコード */
static UINT8	s_locked[0x80];			/* ロック式モディファイアの状態 */

int softkbd_isvisible(void) {

	return(s_visible);
}

void softkbd_toggle(void) {

	s_visible ^= 1;
	if (!s_visible) {
		int i;
		if (s_pressed != 0xff) {
			keystat_keyup(s_pressed);
			s_pressed = 0xff;
		}
		for (i = 0; i < 0x80; i++) {
			if (s_locked[i]) {
				s_locked[i] = 0;
				keystat_keyup((REG8)i);
			}
		}
	}
}

static int islockkey(UINT8 code) {

	return((code == NKEY_SHIFT) || (code == NKEY_CTRL) ||
			(code == NKEY_KANA) || (code == NKEY_GRPH) ||
			(code == NKEY_CAPS));
}

void softkbd_move(int dx, int dy) {

	if (dy) {
		s_row = (s_row + dy + NROWS) % NROWS;
		if (s_col >= rowlen[s_row]) {
			s_col = rowlen[s_row] - 1;
		}
	}
	if (dx) {
		s_col = (s_col + dx + rowlen[s_row]) % rowlen[s_row];
	}
}

void softkbd_press(void) {

	UINT8 code = rows[s_row][s_col].code;

	if (islockkey(code)) {
		s_locked[code] ^= 1;
		if (s_locked[code]) {
			keystat_keydown(code);
		}
		else {
			keystat_keyup(code);
		}
		return;
	}
	if (s_pressed == 0xff) {
		s_pressed = code;
		keystat_keydown(code);
	}
}

void softkbd_release(void) {

	if (s_pressed != 0xff) {
		keystat_keyup(s_pressed);
		s_pressed = 0xff;
	}
}

/* ---- 描画 (dst = 640 ストライドの RGB565、画面下部に重ねる) ---- */

#define	COL_BG		0x18e3		/* 暗灰 */
#define	COL_KEY		0x39e7
#define	COL_SEL		0xffff		/* 選択中: 白地 */
#define	COL_LOCK	0x07e0		/* ロック中: 緑 */
#define	COL_TEXT	0xffff
#define	COL_TEXTSEL	0x0000

/* 3x5 ピクセルフォント (各行 3bit、上から 5 行)。fontmng は RESOURCE_US
 * ビルドで使えないため自前で持つ。 */
static const char skb_chars[] =
	"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-^\\@[];:,./_<>v";
static const UINT8 skb_glyph[][5] = {
	{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
	{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
	{2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},
	{7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},
	{5,6,4,6,5},{4,4,4,4,7},{5,7,5,5,5},{6,5,5,5,5},{2,5,5,5,2},
	{6,5,6,4,4},{2,5,5,6,3},{6,5,6,6,5},{3,4,2,1,6},{7,2,2,2,2},
	{5,5,5,5,7},{5,5,5,5,2},{5,5,5,7,5},{5,5,2,5,5},{5,5,2,2,2},
	{7,1,2,4,7},{0,0,7,0,0},{2,5,0,0,0},{4,4,2,1,1},{2,5,7,4,3},
	{3,2,2,2,3},{6,2,2,2,6},{0,2,0,2,4},{0,2,0,2,0},{0,0,0,2,4},
	{0,0,0,0,2},{1,1,2,4,4},{0,0,0,0,7},{1,2,4,2,1},{4,2,1,2,4},
	{0,5,5,5,2}};

void skb_fillrect(UINT16 *dst, int x, int y, int w, int h, UINT16 c) {

	int	i, j;

	for (j = 0; j < h; j++) {
		UINT16 *p = dst + (y + j) * 640 + x;
		for (i = 0; i < w; i++) {
			p[i] = c;
		}
	}
}

/* 3x5 フォントで文字列を描く (scale 倍拡大)。大文字化して描画 */
void skb_drawtext(UINT16 *dst, int x, int y, const char *s, UINT16 c, int scale) {

	for (; *s != '\0'; s++, x += 4 * scale) {
		const char	*f;
		const UINT8	*g;
		char		ch;
		int			row, col, ry, rx;

		ch = *s;
		if ((ch >= 'a') && (ch <= 'z')) {
			ch = (char)(ch - 'a' + 'A');
		}
		if (ch == ' ') {
			continue;
		}
		f = strchr(skb_chars, ch);
		if (f == NULL) {
			continue;
		}
		g = skb_glyph[f - skb_chars];
		for (row = 0; row < 5; row++) {
			for (ry = 0; ry < scale; ry++) {
				UINT16 *p = dst + (y + row * scale + ry) * 640 + x;
				for (col = 0; col < 3; col++) {
					if (g[row] & (4 >> col)) {
						for (rx = 0; rx < scale; rx++) {
							p[col * scale + rx] = c;
						}
					}
				}
			}
		}
	}
}

static void drawlabel(UINT16 *dst, int x, int y, const char *s, UINT16 c) {

	skb_drawtext(dst, x, y, s, c, 2);
}

void softkbd_draw(UINT16 *dst) {

	int	r, c;

	if (!s_visible) {
		return;
	}
	skb_fillrect(dst, KBD_X, KBD_Y, KBD_W, KBD_H, COL_BG);
	for (r = 0; r < NROWS; r++) {
		int x = KBD_X + 1;
		for (c = 0; c < rowlen[r]; c++) {
			const SKEY	*k = &rows[r][c];
			int			w = k->w * KEYW - 1;
			int			y = KBD_Y + r * KEYH + 1;
			UINT16		bg, fg;

			if ((r == s_row) && (c == s_col)) {
				bg = COL_SEL;
				fg = COL_TEXTSEL;
			}
			else if (islockkey(k->code) && s_locked[k->code]) {
				bg = COL_LOCK;
				fg = COL_TEXTSEL;
			}
			else {
				bg = COL_KEY;
				fg = COL_TEXT;
			}
			skb_fillrect(dst, x, y, w, KEYH - 1, bg);
			drawlabel(dst, x + 2, y + 2, k->label, fg);
			x += k->w * KEYW;
		}
	}
}
