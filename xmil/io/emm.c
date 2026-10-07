/**
 * @file	emm.c
 * @brief	EMM (拡張メモリボード)
 *
 * I/O 0x0D00-0x0D02 に 24bit アドレス (下位/中位/上位)、0x0D03 がデータ。
 * データの読み書きごとにアドレスが 1 進む。MAME の x1.cpp と同じ挙動。
 * 0x0D04 以降 (EMM BASIC 領域) は未実装で 0xff を返す。
 * 容量は EMM_SIZE (1MB)。範囲外のアドレスは折り返す。
 */

#include	"compiler.h"
#include	"iocore.h"

#define	EMM_SIZE	(1024 * 1024)

static	UINT8	emm_ram[EMM_SIZE];
static	UINT32	emm_addr;

void emm_reset(void) {

	emm_addr = 0;
}

void IOOUTCALL emm_o(UINT port, REG8 dat) {

	switch(port & 0xff) {
		case 0:
			emm_addr = (emm_addr & 0xffff00) | dat;
			break;

		case 1:
			emm_addr = (emm_addr & 0xff00ff) | ((UINT32)dat << 8);
			break;

		case 2:
			emm_addr = (emm_addr & 0x00ffff) | ((UINT32)dat << 16);
			break;

		case 3:
			emm_ram[emm_addr & (EMM_SIZE - 1)] = dat;
			emm_addr = (emm_addr + 1) & 0xffffff;
			break;
	}
}

REG8 IOINPCALL emm_i(UINT port) {

	REG8	ret;

	if ((port & 0xff) != 3) {
		return(0xff);
	}
	ret = emm_ram[emm_addr & (EMM_SIZE - 1)];
	emm_addr = (emm_addr + 1) & 0xffffff;
	return(ret);
}
