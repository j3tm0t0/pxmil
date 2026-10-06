#ifndef PXMIL_PSP_PSPMENU_H
#define PXMIL_PSP_PSPMENU_H

#ifdef __cplusplus
extern "C" {
#endif

int pspmenu_isopen(void);
void pspmenu_toggle(void);
void pspmenu_input(int dx, int dy, int decide, int back);
void pspmenu_draw(UINT16 *dst);
void pspmenu_setmounted(int drive, const char *name);

#ifdef __cplusplus
}
#endif

#endif
