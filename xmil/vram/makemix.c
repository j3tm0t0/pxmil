#include	"compiler.h"
#include	"vram.h"
#include	"scrndraw.h"
#include	"makesub.h"


// 遅いのが嫌なら後でインラインにすればいい
void makemix_mixtext(UINT8 *dst, UINT align, const UINT8 *txt, UINT count) {

	UINT32	datl;
	UINT32	datr;
	REG8	dat;

	do {
		datl = (*(UINT32 *)(dst + 0)) & 0x07070707;
		datr = (*(UINT32 *)(dst + 4)) & 0x07070707;
		dat = txt[MAKETEXT_ROW * 0];
		datl |= TO256COLL(dat, 3);
		datr |= TO256COLR(dat, 3);
		dat = txt[MAKETEXT_ROW * 1];
		datl |= TO256COLL(dat, 4);
		datr |= TO256COLR(dat, 4);
		dat = txt[MAKETEXT_ROW * 2];
		datl |= TO256COLL(dat, 5);
		datr |= TO256COLR(dat, 5);
		*(UINT32 *)(dst + 0) = datl;
		*(UINT32 *)(dst + 4) = datr;
		txt++;
		dst += align;
	} while(--count);
}

void makemix_mixgrph(UINT8 *dst, UINT align, const UINT8 *grp, UINT count) {

	UINT	pos;
	UINT32	datl;
	UINT32	datr;
	REG8	dat;

	pos = 0;
	do {
		datl = (*(UINT32 *)(dst + 0)) & 0x38383838;
		datr = (*(UINT32 *)(dst + 4)) & 0x38383838;
		dat = grp[pos + GRAM_B];
		datl |= TO256COLL(dat, 0);
		datr |= TO256COLR(dat, 0);
		dat = grp[pos + GRAM_R];
		datl |= TO256COLL(dat, 1);
		datr |= TO256COLR(dat, 1);
		dat = grp[pos + GRAM_G];
		datl |= TO256COLL(dat, 2);
		datr |= TO256COLR(dat, 2);
		*(UINT32 *)(dst + 0) = datl;
		*(UINT32 *)(dst + 4) = datr;
		pos = (pos + GRAM_LINESTEP) & (GRAM_LINESTEP * 7);
		dst += align;
	} while(--count);
}

#if defined(SUPPORT_TURBOZ)
/* turboZ 64色 (6プレーン) モードのセル展開。
   bank0(grp0) の B/R/G プレーン = 6bit インデックスの bit0/1/2、
   bank1(grp1) 縺ｮ B/R/G = bit3/4/5縲ゅ％縺ｮ荳ｦ縺ｳ縺ｯ crtc.c palette_o(64濶ｲ)縺ｮ
   インデックス計算 (bit0..2=bank0 BRG, bit3..5=bank1 BRG) と pal4096banktbl に
   一致する。screenmap の各バイトに 0..63 のインデックスを書く(テキスト混合なし)。 */
void makemix_mixgrph64(UINT8 *dst, UINT align,
					const UINT8 *grp0, const UINT8 *grp1, UINT count) {

	UINT	pos;
	UINT32	datl;
	UINT32	datr;
	REG8	dat;

	pos = 0;
	do {
		datl = 0;
		datr = 0;
		dat = grp0[pos + GRAM_B];
		datl |= TO256COLL(dat, 0);
		datr |= TO256COLR(dat, 0);
		dat = grp0[pos + GRAM_R];
		datl |= TO256COLL(dat, 1);
		datr |= TO256COLR(dat, 1);
		dat = grp0[pos + GRAM_G];
		datl |= TO256COLL(dat, 2);
		datr |= TO256COLR(dat, 2);
		dat = grp1[pos + GRAM_B];
		datl |= TO256COLL(dat, 3);
		datr |= TO256COLR(dat, 3);
		dat = grp1[pos + GRAM_R];
		datl |= TO256COLL(dat, 4);
		datr |= TO256COLR(dat, 4);
		dat = grp1[pos + GRAM_G];
		datl |= TO256COLL(dat, 5);
		datr |= TO256COLR(dat, 5);
		*(UINT32 *)(dst + 0) = datl;
		*(UINT32 *)(dst + 4) = datr;
		pos = (pos + GRAM_LINESTEP) & (GRAM_LINESTEP * 7);
		dst += align;
	} while(--count);
}

/* 64-color mode text/PCG overlay: where a glyph pixel has a non-zero
   3bit color (tc 1..7), overwrite the screenmap with pen 0x40|tc
   (text pens live at pal index 64..71 just above the 64 graphics
   pens, so tc=0 stays transparent and the graphics pixel shows). */
void makemix_mixtext64(UINT8 *dst, UINT align, const UINT8 *txt, UINT count) {

	UINT	x;
	REG8	p0;
	REG8	p1;
	REG8	p2;
	REG8	m;
	REG8	tc;

	do {
		p0 = txt[MAKETEXT_ROW * 0];
		p1 = txt[MAKETEXT_ROW * 1];
		p2 = txt[MAKETEXT_ROW * 2];
		m = 0x80;
		for (x = 0; x < 8; x++) {
			tc = 0;
			if (p0 & m) { tc |= 1; }
			if (p1 & m) { tc |= 2; }
			if (p2 & m) { tc |= 4; }
			if (tc) {
				dst[x] = (UINT8)(0x40 | tc);
			}
			m >>= 1;
		}
		txt++;
		dst += align;
	} while(--count);
}
#endif


void makemix_settext(UINT8 *dst, UINT align, const UINT8 *txt, UINT count) {

	REG8	dat;
	UINT32	datl;
	UINT32	datr;

	do {
		dat = txt[MAKETEXT_ROW * 0];
		datl = TO256COLL(dat, 3);
		datr = TO256COLR(dat, 3);
		dat = txt[MAKETEXT_ROW * 1];
		datl |= TO256COLL(dat, 4);
		datr |= TO256COLR(dat, 4);
		dat = txt[MAKETEXT_ROW * 2];
		datl |= TO256COLL(dat, 5);
		datr |= TO256COLR(dat, 5);
		*(UINT32 *)(dst + 0) = datl;
		*(UINT32 *)(dst + 4) = datr;
		txt++;
		dst += align;
	} while(--count);
}

void makemix_ul20(UINT8 *dst, UINT pos) {

	UINT32	dat;

	dat = (TRAM_KNJ(pos) & TRAMKNJ_ULINE)?0x01010101:0x00000000;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 0) + 0) = dat;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 0) + 4) = dat;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 1) + 0) = dat;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 1) + 4) = dat;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 2) + 0) = 0;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 2) + 4) = 0;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 3) + 0) = 0;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 3) + 4) = 0;
}

void makemix_ul10(UINT8 *dst, UINT pos) {

	UINT32	dat;

	dat = (TRAM_KNJ(pos) & TRAMKNJ_ULINE)?0x01010101:0x00000000;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 0) + 0) = dat;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 0) + 4) = dat;
//	*(UINT32 *)(dst + (SURFACE_WIDTH * 1) + 0) = dat;
//	*(UINT32 *)(dst + (SURFACE_WIDTH * 1) + 4) = dat;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 2) + 0) = dat;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 2) + 4) = dat;
//	*(UINT32 *)(dst + (SURFACE_WIDTH * 3) + 0) = dat;
//	*(UINT32 *)(dst + (SURFACE_WIDTH * 3) + 4) = dat;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 4) + 0) = 0;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 4) + 4) = 0;
//	*(UINT32 *)(dst + (SURFACE_WIDTH * 5) + 0) = 0;
//	*(UINT32 *)(dst + (SURFACE_WIDTH * 5) + 4) = 0;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 6) + 0) = 0;
	*(UINT32 *)(dst + (SURFACE_WIDTH * 6) + 4) = 0;
//	*(UINT32 *)(dst + (SURFACE_WIDTH * 7) + 0) = 0;
//	*(UINT32 *)(dst + (SURFACE_WIDTH * 7) + 4) = 0;
}


void makemix_cpy200(UINT8 *dst, UINT pos, UINT count) {

	count -= pos;
	pos = pos * SURFACE_WIDTH * 2;
	do {
		*(UINT32 *)(dst + pos + 0) = *(UINT32 *)(dst + 0);
		*(UINT32 *)(dst + pos + 4) = *(UINT32 *)(dst + 4);
		dst += SURFACE_WIDTH * 2;
	} while(--count);
}

void makemix_cpy400(UINT8 *dst, UINT pos, UINT count) {

	count -= pos;
	pos = pos * SURFACE_WIDTH;
	do {
		*(UINT32 *)(dst + pos + 0) = *(UINT32 *)(dst + 0);
		*(UINT32 *)(dst + pos + 4) = *(UINT32 *)(dst + 4);
		dst += SURFACE_WIDTH;
	} while(--count);
}

