/* Mac ネイティブビルド (Makefile.macos) 用グルー。
 * PSP フロントエンドや libretro が別途定義しているシンボルを補う。 */
#include "compiler.h"

/* vram/palettes.c が参照 (libretro では libretro.c が定義) */
int allow_scanlines = 0;

/* z80c/z80c.mcr の Z80_COUNT が参照する CPU サイクル倍率 (256 = 等倍)。
 * PSP 版は pspmenu.c が定義。ここでは等倍固定。 */
UINT32 z80_cycmul = 256;

/* SDL2 は compiler.h 経由で main を SDL_main に、
 * フロントエンドの入口は xmil_main なので中継する。 */
extern int xmil_main(int argc, char *argv[]);
int main(int argc, char *argv[]) {
	/* XMIL_CYCMUL=128 で 8MHz, 256 で 4MHz (既定) を選べるようにする。
	 * DMA/CPU の両方に倍率が掛かる (crtc/dmac 側で参照)。 */
	const char *cm = getenv("XMIL_CYCMUL");
	if (cm) {
		long v = atol(cm);
		if (v >= 1 && v <= 4096) {
			z80_cycmul = (UINT32)v;
		}
	}
	return xmil_main(argc, argv);
}

/* turboZ カラーモード切替。sdl2 フロントは未実装なので常に成功
 * (16bpp 直描き前提。PSP 版と同じ扱い)。 */
BRESULT scrnmng_setcolormode(BOOL fullcolor) {
	(void)fullcolor;
	return SUCCESS;
}
