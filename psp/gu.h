#ifndef PXMIL_PSP_GU_H
#define PXMIL_PSP_GU_H

#ifdef __cplusplus
extern "C" {
#endif

void pxgu_init(void);
void pxgu_set_overlay(const char *text);
void pxgu_flush(void);
void pxgu_present(const UINT16 *src, int srcw, int srch,
				int dstx, int dsty, int dstw, int dsth);

#ifdef __cplusplus
}
#endif

#endif
