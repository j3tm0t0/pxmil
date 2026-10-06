#ifndef PXMIL_PSP_PERF_H
#define PXMIL_PSP_PERF_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	UINT32	execps;		/* pccore_exec 回数/秒 */
	UINT32	drawps;		/* present 回数/秒 (= 表示 fps) */
	UINT32	execus;		/* pccore_exec 平均 µs */
	UINT32	presus;		/* present_frame 平均 µs */
} PERFNOW;

extern PERFNOW	perf_now;

UINT32 perf_us(void);
void perf_add_exec(UINT32 us);
void perf_add_present(UINT32 us);
void perf_tick(void);
void perf_dump(void);

#ifdef __cplusplus
}
#endif

#endif
