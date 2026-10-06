/**
 * @file	gu.c
 * @brief	GU による画面表示: 640x400 RGB565 のソースをテクスチャとして
 *			480x272 へ縮小描画する
 *
 * SDL_BlitScaled (CPU) の置き換え。テクスチャ幅は 2 のべき乗制限が
 * あるため、640 幅のソースは 512 + 128 の 2 ストリップに分けて描く
 * (px68k gecomp.c と同じ手法)。バッファ幅 (ストライド) は 640 のまま
 * でよい。バイリニア補間つきで、ダブルバッファを sceGuSwapBuffers で
 * アトミックに切り替えるため中間状態が画面に出ることはない。
 */

#include	"compiler.h"
#include	<pspkernel.h>
#include	<pspdisplay.h>
#include	<pspgu.h>

#include	"gu.h"

#define	BUF_WIDTH	512
#define	SCR_WIDTH	480
#define	SCR_HEIGHT	272
#define	FRAME_SIZE	(BUF_WIDTH * SCR_HEIGHT * 2)	/* RGB565 */

static unsigned int __attribute__((aligned(64))) s_list[4096];
static int	s_init;

typedef struct {
	float	u, v;
	float	x, y, z;
} VERTEX;

void pxgu_init(void) {

	if (s_init) {
		return;
	}
	sceGuInit();
	sceGuStart(GU_DIRECT, s_list);
	sceGuDrawBuffer(GU_PSM_5650, (void *)0, BUF_WIDTH);
	sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void *)FRAME_SIZE, BUF_WIDTH);
	sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
	sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
	sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuDisable(GU_DEPTH_TEST);
	sceGuDepthMask(GU_TRUE);		/* 深度は書かない */
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_5650, 0, 0, GU_FALSE);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuFinish();
	sceGuSync(0, 0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);
	s_init = 1;
}

/* 1 ストリップを dst に対応する範囲へ描く */
static void draw_strip(const UINT16 *tex, int texw, int texh,
						float u0, float u1,
						float x0, float x1, float y0, float y1) {

	VERTEX	*v;

	sceGuTexImage(0, texw, 512, 640, tex);
	v = (VERTEX *)sceGuGetMemory(2 * sizeof(VERTEX));
	v[0].u = u0;	v[0].v = 0.0f;
	v[0].x = x0;	v[0].y = y0;	v[0].z = 0.0f;
	v[1].u = u1;	v[1].v = (float)texh;
	v[1].x = x1;	v[1].y = y1;	v[1].z = 0.0f;
	sceGuDrawArray(GU_SPRITES,
		GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, v);
}

/*
 * src: RGB565、ストライド 640 ピクセル。srcw x srch を
 * (dstx, dsty) - (dstx+dstw, dsty+dsth) へ拡縮して描画し、スワップする。
 */
void pxgu_present(const UINT16 *src, int srcw, int srch,
				int dstx, int dsty, int dstw, int dsth) {

	float	sx;
	float	x0, y0, x1, y1;

	if (!s_init) {
		return;
	}
	sceKernelDcacheWritebackRange(src, 640 * srch * 2);

	sceGuStart(GU_DIRECT, s_list);
	sceGuClearColor(0);
	sceGuClear(GU_COLOR_BUFFER_BIT);

	sx = (float)dstw / (float)srcw;
	x0 = (float)dstx;
	y0 = (float)dsty;
	y1 = (float)(dsty + dsth);
	if (srcw > 512) {
		x1 = x0 + 512.0f * sx;
		draw_strip(src, 512, srch, 0.0f, 512.0f, x0, x1, y0, y1);
		draw_strip(src + 512, 128, srch, 0.0f, (float)(srcw - 512),
					x1, (float)(dstx + dstw), y0, y1);
	}
	else {
		draw_strip(src, 512, srch, 0.0f, (float)srcw,
					x0, (float)(dstx + dstw), y0, y1);
	}

	sceGuFinish();
	sceGuSync(0, 0);
	sceGuSwapBuffers();
}
