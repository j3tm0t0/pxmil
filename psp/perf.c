/**
 * @file	perf.c
 * @brief	性能計測: pccore_exec / present_frame の所要時間を毎秒集計する
 *
 * メインループが perf_tick() を毎周呼ぶ。1 秒ごとにスナップショットを
 * リングバッファへ記録し、perf_dump() (終了時) で pxmil.log に書き出す。
 * 最新の値は perf_now に入っていて、scrnmng のオーバーレイが表示する。
 * 時刻源は sceKernelGetSystemTimeLow() (µs、32bit ラップは集計窓 1 秒
 * なので無視できる)。
 */

#include	"compiler.h"
#include	<pspthreadman.h>
#include	"dosio.h"
#include	"perf.h"

PERFNOW	perf_now;

/* 集計中の 1 秒ぶん */
static UINT32	cur_execus;
static UINT32	cur_presus;
static UINT32	cur_execcnt;
static UINT32	cur_drawcnt;
static UINT32	cur_loopcnt;
static UINT32	win_start;		/* 集計窓の開始 (ms, GETTICK) */

/* 1 行 = 1 秒。リングに溜めて終了時に書き出す */
#define	PERF_RING	600
typedef struct {
	UINT32	sec;
	UINT16	execcnt;
	UINT16	drawcnt;
	UINT32	execus;		/* 合計 */
	UINT32	presus;		/* 合計 */
	UINT32	loopcnt;
} PERFLINE;
static PERFLINE	ring[PERF_RING];
static UINT		ring_n;			/* 書き込んだ総行数 */

UINT32 perf_us(void) {

	return(sceKernelGetSystemTimeLow());
}

void perf_add_exec(UINT32 us) {

	cur_execus += us;
	cur_execcnt++;
}

void perf_add_present(UINT32 us) {

	cur_presus += us;
	cur_drawcnt++;
}

void perf_tick(void) {

	UINT32	now;
	PERFLINE	*l;

	cur_loopcnt++;
	now = GETTICK();
	if (win_start == 0) {
		win_start = now;
		return;
	}
	if ((now - win_start) < 1000) {
		return;
	}
	win_start = now;

	perf_now.execps = cur_execcnt;
	perf_now.drawps = cur_drawcnt;
	perf_now.execus = (cur_execcnt) ? (cur_execus / cur_execcnt) : 0;
	perf_now.presus = (cur_drawcnt) ? (cur_presus / cur_drawcnt) : 0;

	l = &ring[ring_n % PERF_RING];
	l->sec = now / 1000;
	l->execcnt = (UINT16)cur_execcnt;
	l->drawcnt = (UINT16)cur_drawcnt;
	l->execus = cur_execus;
	l->presus = cur_presus;
	l->loopcnt = cur_loopcnt;
	ring_n++;

	cur_execus = 0;
	cur_presus = 0;
	cur_execcnt = 0;
	cur_drawcnt = 0;
	cur_loopcnt = 0;
}

/* pxmil.log に CSV で書き出す (EBOOT と同じディレクトリ) */
void perf_dump(void) {

	FILEH	fh;
	char	buf[128];
	UINT	i, first, n;

	fh = file_create(file_getcd("pxmil.log"));
	if (fh == FILEH_INVALID) {
		return;
	}
	n = (ring_n < PERF_RING) ? ring_n : PERF_RING;
	first = ring_n - n;
	sprintf(buf, "sec,exec/s,draw/s,execus_sum,presus_sum,loop/s\n");
	file_write(fh, buf, (UINT)strlen(buf));
	for (i = 0; i < n; i++) {
		const PERFLINE *l = &ring[(first + i) % PERF_RING];
		sprintf(buf, "%u,%u,%u,%u,%u,%u\n",
				(unsigned)l->sec, l->execcnt, l->drawcnt,
				(unsigned)l->execus, (unsigned)l->presus,
				(unsigned)l->loopcnt);
		file_write(fh, buf, (UINT)strlen(buf));
	}
	file_close(fh);
}
