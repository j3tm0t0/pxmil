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
void pspmenu_applyclock(void);
extern UINT8 pspcfg_clockmul;
extern UINT8 pspcfg_keymode;
void keypad_releaseall(void);
BOOL pspmenu_mountlast(void);
void pspmenu_savegamecfg(void);
extern char pspcfg_fdd0[32];
extern char pspcfg_fdd1[32];

#ifdef __cplusplus
}
#endif

#endif
