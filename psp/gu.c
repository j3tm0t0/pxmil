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
#include	"softkbd.h"

#define	BUF_WIDTH	512
#define	SCR_WIDTH	480
#define	SCR_HEIGHT	272
#define	FRAME_SIZE	(BUF_WIDTH * SCR_HEIGHT * 2)	/* RGB565 */
/* ソース画像の VRAM ステージング先 (フレームバッファ 2 面の直後)。
 * GE は RAM 上の非スウィズルテクスチャを読むのが遅いため、CopyImage で
 * VRAM へ DMA してから VRAM をテクスチャとして描く。640x400x2=512KB。 */
#define	TEXBUF_OFF	(2 * FRAME_SIZE)

static unsigned int __attribute__((aligned(64))) s_list[4096];
static int	s_init;
static int	s_frame;			/* swap 回数 (描画バッファの判定用) */
static char	s_ovltext[16];		/* ネイティブ座標の固定サイズオーバーレイ */

void pxgu_set_overlay(const char *text) {

	int i;
	for (i = 0; (i < 15) && text[i]; i++) {
		s_ovltext[i] = text[i];
	}
	s_ovltext[i] = '\0';
}

typedef struct {
	float	u, v;
	float	x, y, z;
} VERTEX;

static int	s_pending;		/* 発行済み・未スワップのフレームがある */

/* 発行済みフレームを完了待ちして表示へ回す。メニュー等、次の present を
 * 待たずに今の絵をすぐ出したいときにも使う。 */
void pxgu_flush(void) {

	if (!s_pending) {
		return;
	}
	sceGuSync(0, 0);
	if (s_ovltext[0]) {
		UINT32 off = ((s_frame - 1) & 1) ? FRAME_SIZE : 0;
		skb_drawtext_s((UINT16 *)(0x44000000 | off), BUF_WIDTH,
						2, 2, s_ovltext, 0xffff, 2);
	}
	sceGuSwapBuffers();
	s_pending = 0;
}

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
	/* 既定の REPEAT だとバイリニアが左端で右端を回り込んでサンプリング
	 * し、左 1 ラインにゴミが出る (実機で確認)。 */
	sceGuTexWrap(GU_CLAMP, GU_CLAMP);
	sceGuFinish();
	sceGuSync(0, 0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);
	s_init = 1;
}

/* 1 ストリップを dst に対応する範囲へ、幅 SLICE_W の縦スライスに
 * 分割して描く。RAM 上の非スウィズルテクスチャを 1 枚の大きな
 * スプライトで描くと GE のテクスチャキャッシュが効かず激遅になる
 * (実機計測で 25ms/フレーム)。細い縦帯に分けるのが PSP の定石。 */
#define	SLICE_W		32

static void draw_strip(const UINT16 *tex, int texw, int texh,
						float u0, float u1,
						float x0, float x1, float y0, float y1) {

	VERTEX	*v;
	float	u, du, x, dx;
	int		n, i;

	sceGuTexImage(0, texw, 512, 640, tex);
	n = (int)((u1 - u0) + SLICE_W - 1) / SLICE_W;
	if (n < 1) {
		n = 1;
	}
	du = (u1 - u0) / n;
	dx = (x1 - x0) / n;
	v = (VERTEX *)sceGuGetMemory(2 * n * sizeof(VERTEX));
	u = u0;
	x = x0;
	for (i = 0; i < n; i++) {
		v[i * 2].u = u;			v[i * 2].v = 0.0f;
		v[i * 2].x = x;			v[i * 2].y = y0;	v[i * 2].z = 0.0f;
		u += du;
		x += dx;
		v[i * 2 + 1].u = u;		v[i * 2 + 1].v = (float)texh;
		v[i * 2 + 1].x = x;		v[i * 2 + 1].y = y1;	v[i * 2 + 1].z = 0.0f;
	}
	sceGuDrawArray(GU_SPRITES,
		GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2 * n, NULL, v);
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

	/* パイプライン: ここで「前フレーム」の GE 完了を待って表示へ回し、
	 * 今フレームはコマンド発行だけして戻る (GE はエミュレーションと
	 * 並行して描く)。表示は 1 フレーム遅れるが CPU の待ちが消える。 */
	pxgu_flush();

	sceKernelDcacheWritebackRange(src, 640 * srch * 2);

	sceGuStart(GU_DIRECT, s_list);
	/* CPU が書き換えたテクスチャを使うため GE のテクスチャキャッシュを
	 * 破棄する。無いと実機で古いキャッシュラインが混ざってゴミになる
	 * (PPSSPP はテクスチャキャッシュを再現しないので出ない)。 */
	sceGuTexFlush();
	/* CLAMP は毎フレーム設定する。init で一度設定するだけでは実機で
	 * 保持されず REPEAT に戻り、バイリニアが u=0 で右端を回り込んで
	 * 画像左端に右端の内容が 1 ラインぶん出る (実機のみ再現)。 */
	sceGuTexWrap(GU_CLAMP, GU_CLAMP);
	sceGuClearColor(0);
	sceGuClear(GU_COLOR_BUFFER_BIT);

	/* ソースを VRAM へ GE DMA してから、VRAM をテクスチャに使う */
	sceGuCopyImage(GU_PSM_5650, 0, 0, 640, srch, 640, (void *)src,
					0, 0, 640, (void *)(0x04000000 + TEXBUF_OFF));
	sceGuTexSync();

	sx = (float)dstw / (float)srcw;
	x0 = (float)dstx;
	y0 = (float)dsty;
	y1 = (float)(dsty + dsth);
	{
		const UINT16 *vtex = (const UINT16 *)(0x04000000 + TEXBUF_OFF);
		if (srcw > 512) {
			x1 = x0 + 512.0f * sx;
			draw_strip(vtex, 512, srch, 0.0f, 512.0f, x0, x1, y0, y1);
			draw_strip(vtex + 512, 128, srch, 0.0f, (float)(srcw - 512),
						x1, (float)(dstx + dstw), y0, y1);
		}
		else {
			draw_strip(vtex, 512, srch, 0.0f, (float)srcw,
						x0, (float)(dstx + dstw), y0, y1);
		}
	}

	sceGuFinish();
	s_frame++;
	s_pending = 1;
	/* sync と swap は次回の pxgu_present 冒頭 (または pxgu_flush) で行う */
}
